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

/**
 * @file ClientMessageHandleJob.h
 *
 * @author roman.simakov@gmail.com
 *
 * @brief The job for handling messages from client.
 *
 * The main difference between handling of messages between instances is that we must send
 * response to this message and keep synchronous client connection to do that.
 */

#ifndef CLIENTMESSAGEHANDLEJOB_H_
#define CLIENTMESSAGEHANDLEJOB_H_

#include "MessageHandleJob.h"

#include <util/Notification.h>

#include <memory>
#include <stdint.h>

namespace scidb
{

class Connection;
class QueryResult;
class RemoteMergedArray;
class SciDB;
class ConstChunk;

/**
 * The class created by network message handler for adding to queue to be processed
 * by thread pool and handle message from client.
 */
class ClientMessageHandleJob : public MessageHandleJob
{
    typedef MessageHandleJob inherited;

 public:
    ClientMessageHandleJob(
        const std::shared_ptr< Connection > &connection,
        const std::shared_ptr<MessageDesc>& messageDesc);

    /**
     * Based on its contents this message is prepared and scheduled to run
     * on an appropriate queue.
     * @see MessageHandleJob::dispatch
     */
    void dispatch(NetworkManager* nm) override;

 protected:

    /// Implementation of Job::run()
    /// @see Job::run()
    void run() override;

 private:
    std::shared_ptr<Connection>     _connection;

    std::string getProgramOptions(const std::string &programOptions) const;

    /**
     * Retrieve the combined user-name stored in the
     * session as a string.
     */
    std::string getUserName() const;

    /**
     *  This method processes message mtPrepareQuery containing client query string.
     *  The processing includes parsing and building an execution plan.
     *  When processing is complete a response is sent to client using _connection.
     */
    void prepareClientQuery();

    /**
     * In case of certain exceptions (e.g. LockBusyException),
     * prepareClientQuery() can be re-tried multiple times.
     * This is the method to do so.
     *  @param queryResult is a structure containing the current state of the query
     */
    void retryPrepareQuery(scidb::QueryResult& queryResult);

    /// Helper routine
    void postPrepareQuery(scidb::QueryResult& queryResult);

    /**
     *  This method processes message mtPrepareQuery containing client query string.
     *  The processing includes executing the execution plan.
     *  When processing is complete a response is sent to client using _connection.
     */
    void executeClientQuery();

    /**
     * In case of certain exceptions (e.g. LockBusyException)
     * executeClientQuery() can be re-tried multiple times.
     * This is the method to do so.
     *  @param queryResult is a structure containing the current state of the query
     */
    void retryExecuteQuery(scidb::QueryResult& queryResult);
    /// Helper routine
    void postExecuteQueryInternal(scidb::QueryResult& queryResult,
                                  const std::shared_ptr<Query>& query);
    /// Helper to install a continuation for executing a physical plan on a given query
    void setPhysPlanContinuation(const std::shared_ptr<Query>& query);
    /// Continuation routine for executing a physical plan
    void completeExecuteQuery(const std::shared_ptr<scidb::QueryResult>& queryResultPtr);

    /**
     * This method sends next chunk to the client.
     * It may schedule (serially) fetchMergedChunk() to do the actual work.
     */
    void fetchChunk();
    /**
     * Fetches partial chunks from some/all instances to produce a complete chunk
     * to be sent to the client. It never waits, but reschedules and re-executes itself
     * until a complete chunk is ready or the query is aborted.
     */
    void fetchMergedChunk(std::shared_ptr<RemoteMergedArray>& fetchArray, AttributeID attributeId,
                          Notification<scidb::Exception>::SubscriberID queryErrorSubscriberID);
    /// Helper to construct an mtChunk message for the client
    void populateClientChunk(const std::string& arrayName,
                             AttributeID attributeId,
                             const ConstChunk* chunk,
                             std::shared_ptr<MessageDesc>& chunkMsg);
    /**
     * Used to re-schedule fetchMergedChunk()
     */
    void executeSerially(std::shared_ptr<WorkQueue>& serialQueue,
                         std::weak_ptr<WorkQueue>& initialQueue,
                         const scidb::Exception* error);
    /**
     * Functor used for re-scheduling fetchMergedChunk() in response to various events (e.g. partial chunk arrival)
     */
    typedef std::function<void(const scidb::Exception* error)> RescheduleCallback;
    /**
     * Generate a RescheduleCallback functor
     * @param serialQueue [out] the serial work queue where fetchMergedChunk() is to be executed
     */
    RescheduleCallback getSerializeCallback(std::shared_ptr<WorkQueue>& serialQueue);
    /**
     * Query error event handler
     */
    void handleQueryError(RescheduleCallback& cb,
                          Notification<scidb::Exception>::MessageTypePtr errPtr);

    /// Internal exception used to cancel any outstanding attempts to run fetchMergedChunk()
    DECLARE_SYSTEM_EXC_SUBCLASS_W_ARGS(CancelChunkFetchException,
                                       SCIDB_SE_INTERNAL,
                                       SCIDB_LE_UNKNOWN_ERROR,
                                       "CancelChunkFetchException");
    /**
     * This method cancels query execution and free context
     */
    void cancelQuery();

    /**
     * This method completes the query execution, persists the changes, and frees the context
     */
    void completeQuery();

    /// Helper to deal with exceptions in prepare/executeClientQuery()
    void handleExecuteOrPrepareError(const scidb::Exception& e,
                                     const scidb::QueryResult& queryResult,
                                     scidb::SciDB& scidb);

    /// Helper to deal with exceptions in prepare/executeClientQuery()
    void reportErrorToClient(const Exception& err);

    /// Helper for scheduling this message on a given queue
    void enqueue(const std::shared_ptr<WorkQueue>& q);

    /// Helper for scheduling this message on the error queue of the query
    void enqueueOnErrorQueue(QueryID queryID);

    /// Helper to send a message to the client on _connection
    void sendMessageToClient(std::shared_ptr<MessageDesc>& msg);

    /// Send the newClientComplete message to the client
    void sendNewClientCompleteToClient(
        bool authenticated );

    /// Handle the newClientStart message from the client
    void handleNewClientStart();

    /// This message handles the response from the client to a security
    /// message of type "SecurityMessage" in scidb_msg.proto
    void handleSecurityMessageResponse();

};  // class ClientMessageHandleJob ...

} // namespace

#endif /* CLIENTMESSAGEHANDLEJOB_H_ */
