/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2008-2019 SciDB, Inc.
* All Rights Reserved.
*
* SciDB is free software: you can redistribute it and/or modify
* it under the terms of the AFFERO GNU General Public License as published by
* the Free Software Foundation.
*
* SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the AFFERO GNU General Public License for the complete license terms.
*
* You should have received a copy of the AFFERO GNU General Public License
* along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
*
* END_COPYRIGHT
*/

#include <memory>
#include <string>
#include <vector>

#include <boost/asio.hpp>

#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <malloc.h>
#include <fstream>

#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/helpers/exception.h>

#include <array/ReplicationMgr.h>
#include <ccm/CcmService.h>
#include <dense_linear_algebra/blas/initMathLibs.h>
#include <mpi/MPIManager.h>
#include <network/NetworkManager.h>
#include <query/FunctionLibrary.h>
#include <query/OperatorLibrary.h>
#include <query/Parser.h>
#include <query/QueryProcessor.h>
#include <rbac/NamespaceDesc.h>
#include <rbac/RoleDesc.h>
#include <rbac/UserDesc.h>
#include <system/Config.h>
#include <system/Constants.h>
#include <system/SciDBConfigOptions.h>
#include <system/SystemCatalog.h>
#include <system/Utils.h>
#include <util/InjectedError.h>
#include <util/JobQueue.h>
#include <util/OnScopeExit.h>
#include <util/PathUtils.h>
#include <util/PluginManager.h>
#include <util/RTLog.h>
#include <util/ThreadPool.h>


#ifdef COVERAGE
extern "C" void __gcov_flush(void);
#endif

// to prevent visibility of variable outside of file
static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.entry"));

namespace scidb {namespace arena {bool setMemoryLimit(size_t);}}

using namespace scidb;
using namespace std;

std::shared_ptr<ThreadPool> messagesThreadPool;

std::string printPrefix(); // forward decl

void scidb_termination_handler(int signum)
{
    NetworkManager::shutdown();
}

namespace scidb { namespace arena {
/// @return whether pooled allocation is used.
/// @see the definition in RootArena.cpp.
bool pooledAllocationWrapper();
}}

void registerInstance(const std::string& storageConfigPath,
                      const std::string& address,
                      uint16_t port)
{
    auto selfInstanceID = StorageMgr::getInstance()->getInstanceId();

    if (selfInstanceID != INVALID_INSTANCE) {
        throw USER_EXCEPTION(SCIDB_SE_STORAGE, SCIDB_LE_STORAGE_ALREADY_REGISTERED)
            << Iid(selfInstanceID);
    }

    // storageConfigDir should be of the form <base-dir>/<server-id>/<server-instance-id>
    string storageConfigDir = scidb::getDir(storageConfigPath);
    if (!isFullyQualified(storageConfigDir)) {
        throw (USER_EXCEPTION(SCIDB_SE_STORAGE, SCIDB_LE_NON_FQ_PATH_ERROR) << storageConfigPath);
    }

    stringstream ss;
    uint32_t sid(~0);
    uint32_t siid(~0);

    string serverInstanceId = scidb::getFile(storageConfigDir);
    ss.str(serverInstanceId);
    if (serverInstanceId.empty() || !(ss >> siid) || !ss.eof()) {
        throw (USER_EXCEPTION(SCIDB_SE_STORAGE, SCIDB_LE_INSTANCE_PATH_FORMAT_ERROR) << storageConfigPath);
    }

    string serverInstanceIdDir = scidb::getDir(storageConfigDir);
    if (serverInstanceIdDir.empty() || serverInstanceIdDir=="." || serverInstanceIdDir=="/") {
        throw (USER_EXCEPTION(SCIDB_SE_STORAGE, SCIDB_LE_INSTANCE_PATH_FORMAT_ERROR) << storageConfigPath);
    }
    string serverId = scidb::getFile(serverInstanceIdDir);
    ss.clear();
    ss.str(serverId);
    if (serverId.empty() || !(ss >> sid) || !ss.eof()) {
        throw (USER_EXCEPTION(SCIDB_SE_STORAGE, SCIDB_LE_INSTANCE_PATH_FORMAT_ERROR) << storageConfigPath);
    }

    string basePath = scidb::getDir(serverInstanceIdDir);
    if (serverInstanceIdDir.empty() || serverInstanceIdDir=="." || serverInstanceIdDir=="/") {
        throw (USER_EXCEPTION(SCIDB_SE_STORAGE, SCIDB_LE_INSTANCE_PATH_FORMAT_ERROR) << storageConfigPath);
    }

    LOG4CXX_DEBUG(logger, "server-id = " << sid);
    LOG4CXX_DEBUG(logger, "server-instance-id = " << siid);

    const string online = Config::getInstance()->getOption<string>(CONFIG_ONLINE);
    SystemCatalog* catalog = SystemCatalog::getInstance();
    auto newInstanceID = catalog->addInstance(InstanceDesc(address, port, basePath, sid, siid), online);

    StorageMgr::getInstance()->setInstanceId(newInstanceID);
    LOG4CXX_DEBUG(logger, "Registered physical instance " << Iid(newInstanceID));
}

/*
 * To avoid a "thundering hurd" problem, avoid having all instances
 * speaking to the catalog at the same time.  Stagger their access
 * based on physical instance id.
 */
void delayCatalogWork(InstanceID piid)
{
    // Since the low 32 bits of physical instance id is assigned from
    // a Postgres sequence, we can use the low bits to divide the
    // instances into roughly equal sized groups, and give each group
    // a different delay.  We'll cap the longest delay at five
    // seconds.
    //
    // The catalog uses Query::runRestartableWork() which does do its
    // retries at random intervals, but this apparently isn't enough
    // to prevent lots of transaction conflicts doing
    // invalidateTempArrays() work.  (SDB-5497)

    long max_delay = ::max(2, Config::getInstance()->getOption<int>(CONFIG_X_CATALOG_DELAY_SECS));
    const long MAX_DELAY_NANOS = max_delay * 1000000000L;
    const long PER_GROUP_NANOS = MAX_DELAY_NANOS >> 5; // i.e. x/32
    long nanos = (piid & 0x1F) * PER_GROUP_NANOS;

    LOG4CXX_TRACE(logger, __func__ << ": Pause instance " << Iid(piid)
                  << " for " << nanos << "ns (max " << max_delay
                  << "s, " << PER_GROUP_NANOS << "ns per group)");

    timespec ts { nanos / 1000000000L, nanos % 1000000000L };
    ::nanosleep(&ts, nullptr);

    LOG4CXX_TRACE(logger, __func__ << ": Resuming instance " << Iid(piid));
}

void runSciDB()
{
   LOG4CXX_TRACE(logger, "runSciDB(): start");

   struct sigaction action;
   action.sa_handler = scidb_termination_handler;
   sigemptyset(&action.sa_mask);
   action.sa_flags = 0;
   sigaction (SIGINT, &action, NULL);
   sigaction (SIGTERM, &action, NULL);

   struct sigaction ignore;
   ignore.sa_handler = SIG_IGN;
   sigemptyset(&ignore.sa_mask);
   ignore.sa_flags = 0;
   sigaction (SIGPIPE, &ignore, NULL);

   Config *cfg = Config::getInstance();
   assert(cfg);

   // Force load
   MonitorConfig *mcfg = MonitorConfig::getInstance();
   SCIDB_ASSERT(mcfg);

   // Configure loggers and make first log entry.
   const string& log4cxxProperties = cfg->getOption<string>(CONFIG_LOGCONF);
   if (log4cxxProperties.empty()) {
      log4cxx::BasicConfigurator::configure();
      const string& log_level = cfg->getOption<string>(CONFIG_LOG_LEVEL);
      log4cxx::LoggerPtr rootLogger(log4cxx::Logger::getRootLogger());
      rootLogger->setLevel(log4cxx::Level::toLevel(log_level));
   }
   else {
      log4cxx::PropertyConfigurator::configure(log4cxxProperties.c_str());
   }
   LOG4CXX_INFO(logger, "Start SciDB instance (pid="<<getpid()<<"). " << SCIDB_BUILD_INFO_STRING(". "));

   // Force deciding whether to use pooled allocation, before threads are being used.
   bool usePooled = scidb::arena::pooledAllocationWrapper();
   LOG4CXX_INFO(logger, "Pooled memory allocation: " << (usePooled ? "ON" : "OFF"));

   // log the config
   LOG4CXX_INFO(logger, "Configuration:\n" << cfg->toString());

   // all subsytems with constraints on their config vars
   // should check them here prior to other initializations
   try
   {
       checkConfigNewStorage();
   }
   catch (const std::exception &e)
   {
      LOG4CXX_ERROR(logger, __func__ << ": Failed to initialize storage configuration: " << e.what());
      cerr << printPrefix()
           << "Failed to initialize storage configuration: " << e.what()
           << endl;
      scidb::exit(1);
   }

   // here we are guaranteed outside of mutex, we can read the
   // configuration variable (which uses a mutex) about whether
   // a mutex (and other waits) should be timed
   ScopedWaitTimer::adjustWaitTimingEnabled();

   //Initialize random number generator
   //We will try to read seed from /dev/urandom and if we can't for some reason we will take time and pid as seed
   ifstream file ("/dev/urandom", ios::in|ios::binary);
   unsigned int seed;
   if (file.is_open())
   {
       const size_t size = sizeof(unsigned int);
       char buf[size];
       file.read(buf, size);
       file.close();
       seed = *reinterpret_cast<unsigned int*>(buf);
   }
   else
   {
       seed = static_cast<unsigned int>(time(0) ^ (getpid() << 8));
       LOG4CXX_WARN(logger, "Cannot open /dev/urandom.  Using fallback seed based on time and pid.");
   }
   srandom(seed);

   if (cfg->getOption<int>(CONFIG_MAX_MEMORY_LIMIT) > 0)
   {
       size_t maxMem = ((size_t) cfg->getOption<int>(CONFIG_MAX_MEMORY_LIMIT)) * MiB;
       LOG4CXX_DEBUG(logger, "Capping maximum memory:");

       if (scidb::arena::setMemoryLimit(maxMem))
       {
           LOG4CXX_WARN(logger, ">arena cap set to " << maxMem  << " bytes.");
       }
       else
       {
           LOG4CXX_WARN(logger, ">arena cap of " << maxMem <<
                        " is too small; the cap has already been exceeded.");
       }

       struct rlimit rlim;
       if (getrlimit(RLIMIT_AS, &rlim) != 0)
       {
           LOG4CXX_DEBUG(logger, ">getrlimit call failed: " << ::strerror(errno)
                         << " (" << errno << "); memory cap not set.");
       }
       else
       {
           if (rlim.rlim_cur == RLIM_INFINITY || rlim.rlim_cur > maxMem)
           {
               rlim.rlim_cur = maxMem;
               if (setrlimit(RLIMIT_AS, &rlim) != 0)
               {
                   LOG4CXX_DEBUG(logger, ">setrlimit call failed: " << ::strerror(errno)
                                 << " (" << errno << "); memory cap not set.");
               }
               else
               {
                   LOG4CXX_DEBUG(logger, ">memory cap set to " << rlim.rlim_cur  << " bytes.");
               }
           }
           else
           {
               LOG4CXX_DEBUG(logger, ">memory cap " << rlim.rlim_cur
                             << " is already under " << maxMem << "; not changed.");
           }
       }
   }

   // create temporary directories for data
   LOG4CXX_TRACE(logger, "runSciDB: create temp directories for data");
   string tmpDir = FileManager::getInstance()->getTempDir();
   if (tmpDir.length() == 0 || tmpDir[tmpDir.length()-1] != '/') {
       tmpDir += '/';
   }
   int rc = path::makedirs(tmpDir, 0755, nothrow_t());
   if (rc) {
       LOG4CXX_ERROR(logger, "Cannot create FileManager temp directory "
                     << tmpDir << ": " << ::strerror(rc));
       throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_CANT_OPEN_FILE)
           << tmpDir << ::strerror(rc) << rc;
   }

   LOG4CXX_TRACE(logger, "runSciDB: set large-memalloc-limit");
   int largeMemLimit = cfg->getOption<int>(CONFIG_LARGE_MEMALLOC_LIMIT);
   if (largeMemLimit>0 && (0==mallopt(M_MMAP_MAX, largeMemLimit))) {

       LOG4CXX_WARN(logger, "Failed to set large-memalloc-limit");
   }

   LOG4CXX_TRACE(logger, "runSciDB: set small-memalloc-size");
   size_t smallMemSize = cfg->getOption<size_t>(CONFIG_SMALL_MEMALLOC_SIZE);
   if (smallMemSize>0 && (0==mallopt(M_MMAP_THRESHOLD, safe_static_cast<int>(smallMemSize)))) {
       LOG4CXX_WARN(logger, "Failed to set small-memalloc-size");
   }

   std::shared_ptr<JobQueue> messagesJobQueue = std::make_shared<JobQueue>("messagesJobQueue");

   // Here we can play with thread number
   const size_t nJobs = std::max<size_t>(static_cast<size_t>(cfg->getOption<int>(CONFIG_EXECUTION_THREADS)), 3);
   LOG4CXX_DEBUG(logger, "runSciDB: making job thread pool, nJobs: " << nJobs);
   messagesThreadPool = make_shared<ThreadPool>(nJobs, messagesJobQueue, "messagesThreadPool");

   // During installation, one instance (usually s0-i0) is given the
   // --initialize switch and is responsible for playing the initial
   // SQL setup into the catalog (below).
   SystemCatalog* catalog = SystemCatalog::getInstance();
   const bool initializeCluster = cfg->getOption<bool>(CONFIG_INITIALIZE);
   try
   {
       LOG4CXX_TRACE(logger, "runSciDB: connect to catalog");
       //Disable metadata upgrade in initialize mode
       catalog->connect(!initializeCluster);
   }
   catch (const std::exception &e)
   {
       LOG4CXX_ERROR(logger, "runSciDB: System catalog connection failed: " << e.what());
       scidb::exit(1);
   }

   // During installation, all instances are invoked with the
   // --register switch to register themselves in the catalog instance
   // table.  If we're *not* registering, then we are starting
   // normally (and can know our physical instance id).
   const bool registerMode = cfg->getOption<bool>(CONFIG_REGISTER);
   const bool runMode = not registerMode;

//
// other initializations
//
   int errorCode = 0;
   string phase("initialization");
   ccm::CcmService clientCommunicationService;
   try
   {
       if (!catalog->isInitialized() || initializeCluster)
       {
           LOG4CXX_TRACE(logger, "runSciDB: initialize cluster");
           catalog->initializeCluster();
       }

       // PathUtils pathname expansion initialization.
       path::initialize();

       TypeLibrary::registerBuiltInTypes();

       FunctionLibrary::getInstance()->registerBuiltInFunctions();

       // Force preloading builtin operators
       OperatorLibrary::getInstance();

       LOG4CXX_TRACE(logger, "runSciDB: preloading libraries");
       PluginManager::getInstance()->preLoadLibraries();

       LOG4CXX_TRACE(logger, "runSciDB: preloading macros");
       loadPrelude();  // load built in macros

       // Must have libraries loaded prior to openStorage() and
       // invalidateTempArray(), in case we have to roll back a
       // transaction on an array that lives in a namespace!
       //
       const string& storageConfigPath = cfg->getOption<string>(CONFIG_STORAGE);
       StorageMgr::getInstance()->openStorage(storageConfigPath);
       if (runMode) {
           delayCatalogWork(StorageMgr::getInstance()->getInstanceId());
           catalog->invalidateTempArrays();
       }

       // Pull in the injected error library symbols
       LOG4CXX_TRACE(logger, "runSciDB: preloading injecterror symbols");
       InjectedErrorLibrary::getLibrary()->getError(InjectErrCode::LEGACY_INITIALIZE);
       PhysicalOperator::getInjectedErrorListener();
       ThreadPool::startInjectedErrorListener();

       // (post network setup) configure MPI and run its initialization
       LOG4CXX_TRACE(logger, "runSciDB: MpiManager->init()");
       scidb::MpiManager::getInstance()->init();
       LOG4CXX_TRACE(logger, "runSciDB(): MpiManager::getInstance()->init() done");

       //
       // call main execution loop
       //
       phase = runMode ? "execution" : "registration";

       ReplicationManager::getInstance()->start(messagesJobQueue);
       messagesThreadPool->start();
       const string address = cfg->getOption<string>(CONFIG_HOST);
       const uint16_t port = NetworkManager::getInstance()->getPort();// endPoint.port();
       if (registerMode) {
           registerInstance(storageConfigPath, address, port);
       } else {
           if (not isValidPhysicalInstance(StorageMgr::getInstance()->getInstanceId())) {
               throw USER_EXCEPTION(SCIDB_SE_STORAGE, SCIDB_LE_STORAGE_NOT_REGISTERED);
           }

           LOG4CXX_DEBUG(logger,
                         "Attempting to Start Client Communications Manager : "
                             << Iid(StorageMgr::getInstance()->getInstanceId()));
           // TODO (Fix in Phase 2 of CCM): Starting the Client Communications Manager
           // BEFORE starting the NetworkManager could cause an ordering problem. We are
           // bringing up the Client interface before the Network manager is started. The
           // CcmService communicates with the network manager on the "southbound"
           // side.....
           // TODO (Fix in Phase 2 of CCM): We need some form or ordered broadcast
           //      to specify the port on which instance the CCM Service is running.
           //
           // Right now we just attempt to start the service on every instance. The first
           // instance to bind to the port on each server wins.  Instances which do not
           // win binding to the CCM Port will simply not start up. We end up with one
           // running CCM service on each server in the system.
           clientCommunicationService.start();

           NetworkManager::getInstance()->run(messagesJobQueue,
                                              StorageMgr::getInstance()->getInstanceId(),
                                              address);
       }
   }
   catch (const std::exception &e)
   {
       LOG4CXX_ERROR(logger, __func__ << ": Error during SciDB " << phase
                     << ": " << e.what());
       errorCode = 1;
   }

   //
   // shutdown
   //
   try
   {
      clientCommunicationService.stop();
      Query::freeQueries();
      if (messagesThreadPool) {
         messagesThreadPool->stop();
      }
      ReplicationManager::getInstance()->stop();
   }
   catch (const std::exception &e)
   {
      LOG4CXX_ERROR(logger, "Error during SciDB exit: " << e.what());
      errorCode = 1;
   }
   LOG4CXX_INFO(logger, "SciDB instance. " << SCIDB_BUILD_INFO_STRING(". ") << " is exiting.");
   log4cxx::Logger::getRootLogger()->setLevel(log4cxx::Level::getOff());
   scidb::exit(errorCode);
}

string printPrefix()
{
   stringstream ss;
   time_t t = time(NULL);
   assert(t!=(time_t)-1);
   struct tm tee_em;
   struct tm *date = localtime_r(&t, &tee_em);
   assert(date);
   assert(date == &tee_em);
   if (date) {
       ss << setfill('0')
          << date->tm_year+1900 << "-"
          << setw(2) << date->tm_mon + 1 << "-"
          << setw(2) << date->tm_mday << " "
          << setw(2) << date->tm_hour << ":"
          << setw(2) << date->tm_min << ":"
          << setw(2) << date->tm_sec << " ";
   }
   ss << "(ppid=" << getpid() << "): ";
   return ss.str();
}

void handleFatalError(const int err, const char * msg)
{
    cerr << printPrefix()
         << msg
         << ": " << err
         << ": " << strerror(err)
         << endl;
    scidb::exit(1);
}

int controlPipe[2];

void setupControlPipe()
{
   close(controlPipe[0]);
   close(controlPipe[1]);
   if (pipe(controlPipe)) {
      handleFatalError(errno,"pipe() failed");
   }
}

void checkPort()
{
    uint16_t n = 10;
    while (true) {
        try {
            boost::asio::io_service ioService;
            boost::asio::ip::tcp::acceptor
            testAcceptor(ioService,
                         boost::asio::ip::tcp::endpoint(
                             boost::asio::ip::tcp::v4(),
                             safe_static_cast<uint16_t>(
                                 Config::getInstance()->getOption<int>(CONFIG_PORT))));
            testAcceptor.close();
            ioService.stop();
            return;
        } catch (const boost::system::system_error& e) {
            if ((n--) <= 0) {
                cerr << printPrefix()
                     << e.what() << ". Exiting." << endl;
                scidb::exit(1);
            }
        }
        sleep(1);
    }
}

void terminationHandler(int signum)
{
   unsigned char byte = 1;
   ssize_t ret = write(controlPipe[1], &byte, sizeof(byte));
   if (ret!=1){}
   cerr << printPrefix() << "Terminated." << endl;
   // A signal handler should only call async signal-safe routines
   // _exit() is one, but exit() is not
#ifdef COVERAGE
   __gcov_flush();
#endif
   _exit(0);
}

void initControlPipe()
{
   controlPipe[0] = controlPipe[1] = -1;
}

void setupTerminationHandler()
{
   struct sigaction action;
   action.sa_handler = terminationHandler;
   sigemptyset(&action.sa_mask);
   action.sa_flags = 0;
   sigaction (SIGINT, &action, NULL);
   sigaction (SIGTERM, &action, NULL);
}

void handleExitStatus(int status, pid_t childPid)
{
    if (WIFSIGNALED(status)) {
        cerr << printPrefix()
             << "SciDB child (pid="<<childPid<<") terminated by signal = "
             << WTERMSIG(status) << (WCOREDUMP(status)? ", core dumped" : "")
             << endl;
    }
    if (WIFEXITED(status)) {
        cerr << printPrefix()
             << "SciDB child (pid="<<childPid<<") exited with status = "
             << WEXITSTATUS(status)
             << endl;
    }
}

void runWithWatchdog()
{
   setupTerminationHandler();

   uint32_t forkTimeout = 3; //sec
   uint32_t backOffFactor = 1;
   uint32_t maxBackOffFactor = 32;

   cerr << printPrefix() << "Started." << endl;

   while (true)
   {
      checkPort();
      setupControlPipe();

      time_t forkTime = time(NULL);
      assert(forkTime > 0);

      pid_t pid = ::fork();

      if (pid < 0) { // error
         handleFatalError(errno,"fork() failed");
      } else if (pid > 0) { //parent

         // close the read end of the pipe
         close(controlPipe[0]);
         controlPipe[0] = -1;

         int status;
         pid_t p = wait(&status);
         if (p == -1) {
            handleFatalError(errno,"wait() failed");
         }

         handleExitStatus(status, pid);

         time_t exitTime = time(NULL);
         assert(exitTime > 0);

         if ((exitTime - forkTime) < forkTimeout)
         {
            sleep(backOffFactor*(forkTimeout - static_cast<uint32_t>((exitTime - forkTime))));
            backOffFactor *= 2;
            backOffFactor = (backOffFactor < maxBackOffFactor) ? backOffFactor : maxBackOffFactor;
         } else {
            backOffFactor = 1;
         }

      }  else { //child

         //close the write end of the pipe
         close(controlPipe[1]);
         controlPipe[1] = -1;

         // connect stdin with the read end
         if (dup2(controlPipe[0], STDIN_FILENO) != STDIN_FILENO) {
            handleFatalError(errno,"dup2() failed");
         }
         if (controlPipe[0] != STDIN_FILENO) {
            close(controlPipe[0]);
            controlPipe[0] = -1;
         }

         runSciDB();
         assert(0);
      }
   }
   assert(0);
}

int main(int argc,char* argv[])
{
    RTLog::log("Proof that RTLog::log() is ready for debugging in all scidb builds at any time");

    OnScopeExit onExit([]() {
        log4cxx::Logger::getRootLogger()->setLevel(log4cxx::Level::getOff());
    });

    try
    {
        earlyInitMathLibEnv();  // environ changes must precede multi-threading.
    }
    catch(const std::exception &e)
    {
        cerr << printPrefix()
             << "Failed to initialize math lib environ: " << e.what() << endl;
        scidb::exit(1);
    }

    // need to adjust sigaction SIGCHLD ?
   try
   {
       initConfig(argc, argv); // parse config file
   }
   catch (const std::exception &e)
   {
      cerr << printPrefix()
           << "Failed to initialize server configuration: " << e.what() << endl;
      scidb::exit(1);
   }
   Config *cfg = Config::getInstance();

   if (cfg->getOption<bool>(CONFIG_DAEMON_MODE))
   {
      if (daemon(1, 0) == -1) {
         handleFatalError(errno,"daemon() failed");
      }
      // STDIN is /dev/null in a daemon process,
      // we need to fake it out in case we run without the watchdog
      initControlPipe();
      close(STDIN_FILENO);
      setupControlPipe();
      if (controlPipe[0]==STDIN_FILENO) {
          close(controlPipe[1]);
          controlPipe[1]=-1;
      } else {
          assert(controlPipe[1]==STDIN_FILENO);
          close(controlPipe[0]);
          controlPipe[0]=-1;
      }
   } else {
       initControlPipe();
   }

   if(cfg->getOption<bool>(CONFIG_REGISTER) ||
      cfg->getOption<bool>(CONFIG_NO_WATCHDOG)) {
      runSciDB();
      assert(0);
      scidb::exit(1);
   }
   runWithWatchdog();
   assert(0);
   scidb::exit(1);
}
