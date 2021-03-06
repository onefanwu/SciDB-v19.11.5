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
package org.scidb.jdbc;

/**
 * Interface which exposes some SciDB's features through JDBC
 */
public interface IStatementWrapper
{
    /**
     * Set query execution mode to AFL or language
     * @param afl true - AFL, false - AQL
     */
    public void setAfl(boolean afl);

    /**
     * Return AFL flag
     * @return true if AFL mode
     */
    public boolean isAfl();

    /**
     * Return AQL flag
     * @return true if AQL mode
     */
    public boolean isAql();
}
