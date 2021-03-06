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
 * @file Resources.h
 * @author Artyom Smirnov <smirnoffjr@gmail.com>
 * @brief Transparent interface for examining cluster physical resources
 */

#ifndef RESOURCES_H_
#define RESOURCES_H_

#include <map>

#include <util/Singleton.h>

namespace scidb
{

class BaseResourcesCollector;
using BrcPtr = std::shared_ptr<BaseResourcesCollector>;

/**
 * @brief Class that examine different cluster's physical resources in runtime
 *
 * Class divided into two parts: 1. public interface, which can be accessed from any part of engine,
 * primarily from operators for calling/obtaining result on caller instance. 2. private interface which
 * accessible only from MessageHandleJob object for requesting/collecting result from remote instances.
 *
 */
class Resources: public Singleton<Resources>
{
    static constexpr char const * const cls = "Resources::";

public:
    /**
     * @brief Check file existing on all instances
     *
     * @param[in] path Path to file
     * @param[out] instancesMap Map with physical instance ids and flags of file existence
     * @param[in] query Current query context
     */
    void fileExists(const std::string &path, std::map<InstanceID, bool> &instancesMap,
        const std::shared_ptr<class Query>& query);

    /**
     * @brief Check file existing on single instance
     *
     * @param[in] path Path to file
     * @param[in] liid logical instance ID
     * @param[in] query Current query context
     * @return true if exists
     */
    bool fileExists(const std::string &path, InstanceID liid, const std::shared_ptr<class Query>& query);

private:
    Resources() = default;

    void handleFileExists(const std::shared_ptr<class MessageDesc> &messageDesc);
    bool checkFileExists(const std::string &path) const;
    void markFileExists(uint64_t resourceCollectorId, InstanceID instanceId, bool exists);


    std::map<uint64_t, BrcPtr> _resourcesCollectors;

    Mutex _lock;
    uint64_t _lastResourceCollectorId { 0 };

    friend class Singleton<Resources>;
    friend class ServerMessageHandleJob;
};

}

#endif /* RESOURCES_H_ */
