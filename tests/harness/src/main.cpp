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

/*
 * @file main.cpp
 * @author girish_hilage@persistent.co.in
 */

# include <iostream>
# include <boost/filesystem/operations.hpp>

# include "global.h"
# include "harness.h"
# include "interface.h"
# include "Exceptions.h"

using namespace std;
namespace harnessexceptions = scidbtestharness::Exceptions;
namespace bfs = boost :: filesystem;

/* The following is to remove the TEST_TEMP_DIR if it was generated */
bfs::path temp_test_temp_dir;
void rm_test_temp_dir(void)
{
	if (bfs::exists(temp_test_temp_dir) && bfs::is_directory(temp_test_temp_dir))
		bfs::remove_all(temp_test_temp_dir);
}

int main (int argc, char** argv)
{
	interface::Application *a = new scidbtestharness::SciDBTestHarness;
	int rv;
	stringstream hpid;

	if (atexit(rm_test_temp_dir))
	{
		cerr << "Cannot set exit function" << endl;
		exit(EXIT_FAILURE);
	}

	hpid << (uint64_t) getpid();
	setenv ("HPID", hpid.str().c_str(), 1);

	/* TEST_TEMP_DIR is a temporary directory used in tests */
	/* If TEST_TEMP_DIR is set use its value as a directory path */
	if (getenv("TEST_TEMP_DIR"))
	{
		bfs::path env_test_temp_dir (getenv("TEST_TEMP_DIR"));
		if (bfs::exists(env_test_temp_dir) && !bfs::is_directory(env_test_temp_dir))
		{
			cerr << "TEST_TEMP_DIR=" << env_test_temp_dir.c_str() << " is not a directory" << endl;
			return EXIT_FAILURE;
		}
		if (!bfs::exists(env_test_temp_dir))
		{
			/* Boost will error out if unable to create the directory */
			bfs::create_directories (env_test_temp_dir);
		}
	}
	/* TEST_TEMP_DIR is not set */
	else
	{
		/* Set the environment variable */
		bfs::path env_test_temp_dir = "/tmp/test_";
		env_test_temp_dir += hpid.str().c_str();
		env_test_temp_dir += "_dir";
		setenv("TEST_TEMP_DIR", env_test_temp_dir.c_str(), 1);
		/* Create the directory */
		/* Boost will error out if unable to create the directory */
		bfs::create_directories (env_test_temp_dir);
		/* Note the directory for removal at the end of the run */
		temp_test_temp_dir = env_test_temp_dir;
	}

	try
	{
		if ((rv = a->run (argc, argv, COMMANDLINE)) == FAILURE)
		{
			delete a;
			return EXIT_FAILURE;
		}
	}

	catch (harnessexceptions :: ERROR &e)
	{
		cout << e.what () << endl;
		delete a;
		return EXIT_FAILURE;
	}

    catch (const std::exception& e)
	{
		cout << e.what () << endl;
		delete a;
		return EXIT_FAILURE;
	}

	catch (...)
	{
		cout << "Unhandled Exception caught...\n";
		delete a;
		return EXIT_FAILURE;
	}

	delete a;
	return EXIT_SUCCESS;
}
