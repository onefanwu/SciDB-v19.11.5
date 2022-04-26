#!/usr/bin/python
#
# BEGIN_COPYRIGHT
#
# Copyright (C) 2016-2019 SciDB, Inc.
# All Rights Reserved.
#
# SciDB is free software: you can redistribute it and/or modify
# it under the terms of the AFFERO GNU General Public License as published by
# the Free Software Foundation.
#
# SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
# INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
# NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
# the AFFERO GNU General Public License for the complete license terms.
#
# You should have received a copy of the AFFERO GNU General Public License
# along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
#
# END_COPYRIGHT
#

import argparse
import os
import ply.lex as lex
import ply.yacc as yacc
import sys
import traceback

from ConfigParser import RawConfigParser
from scidblib.scidbctl_common import parse_server_entry


class AppError(Exception):
    """Base class for all exceptions that halt script execution."""
    pass


def parse_disable_file(disable_file=None,
                       new_disable_file=None,
                       config_file=None,
                       instances=None,
                       servers=None,
                       redundancy=None,
                       build=None,
                       security=None,
                       x_dflt_dist_root=None,
                       x_dflt_dist_input=None,
                       x_dflt_dist_other=None,
                       debug=None):

    build_values = ['Assert', 'RelWithDebInfo', 'Debug',
                    'Profile', 'Release', 'Valgrind', 'CC',
                    'DebugNoAssert']
    security_values = ['trust', 'password', 'pam']

    reserved = {
        'instances': 'INSTANCES',
        'servers': 'SERVERS',
        'redundancy': 'REDUNDANCY',
        'build': 'BUILD',
        'x-dflt-dist-root':  'XDFLTDISTROOT',
        'x-dflt-dist-input': 'XDFLTDISTINPUT',
        'x-dflt-dist-other': 'XDFLTDISTOTHER',
        'Assert': 'ASSERT',
        'RelWithDebInfo': 'RELWITHDEBINFO',
        'Debug': 'DEBUG',
        'Profile': 'PROFILE',
        'Release': 'RELEASE',
        'Valgrind': 'VALGRIND',
        'security': 'SECURITY',
        'trust': 'TRUST',
        'password': 'PASSWORD',
        'pam': 'PAM',
        'if': 'IF',
        'unless': 'UNLESS',
        'and': 'AND',
        'or': 'OR'
        }

    tokens = [
        'COMMA',
        'EQUAL',
        'NOT_EQUAL',
        'LESS_THAN',
        'GREATER_THAN',
        'NUMBER',
        'TESTNAME',
        ] + list(reserved.values())

    def t_NUMBER(t):
        r'\d+'
        t.value = int(t.value)
        return t

    def t_TESTNAME(t):
        r'[0-9A-z._/-]+'
        # Check for reserved words
        t.type = reserved.get(t.value, 'TESTNAME')
        return t

    # Regular expression rules for simple tokens
    def t_ignore_COMMENT(t):
        r'\#.*'
        pass

    t_COMMA = r','

    t_EQUAL = r'=='
    t_NOT_EQUAL = r'!='
    t_LESS_THAN = r'<'
    t_GREATER_THAN = r'>'

    # Ignore newlines
    def t_newline(t):
        r'\n+'
        t.lexer.lineno += len(t.value)
        pass

    # A string containing ignored characters (spaces and tabs)
    t_ignore = ' \t'

    # Grammar production rules.  For example given BNF
    #    X ::= Y PLUSOP Z
    # you might
    #    return p[0] = p[1] + p[2]

    start = 'disable'

    # Error handling rule
    def t_error(t):
        error_messages.append(
            "Lexical error at line %d. Illegal character '%s' found."
            % (lexer.lineno, t.value[0]))
        t.lexer.skip(1)

    def p_disable_test_condition_comma(p):
        'disable : TESTNAME COMMA condition_list'
        if p[3]:
            p[0] = p[1]

    def p_disable_test_condition_if(p):
        'disable : TESTNAME IF condition_list'
        if p[3]:
            p[0] = p[1]

    def p_disable_test_condition_unless(p):
        'disable : TESTNAME UNLESS condition_list'
        if p[3] is not True:
            p[0] = p[1]

    def p_disable_test(p):
        'disable : TESTNAME'
        p[0] = p[1]

    def p_disable_(p):
        'disable : '
        pass

    def p_condition_condition_list_comma(p):
        'condition_list : condition COMMA condition_list'
        p[0] = (p[1] and p[3])

    def p_condition_condition_list_and(p):
        'condition_list : condition AND condition_list'
        p[0] = (p[1] and p[3])

    def p_condition_condition_list_or(p):
        'condition_list : condition OR condition_list'
        p[0] = (p[1] or p[3])

    def p_condition_list(p):
        'condition_list : condition'
        p[0] = p[1]

    def p_condition_instances_equal(p):
        'condition : INSTANCES EQUAL NUMBER'
        p[0] = (instances == p[3])

    def p_condition_instances_not_equal(p):
        'condition : INSTANCES NOT_EQUAL NUMBER'
        p[0] = (instances != p[3])

    def p_condition_instances_less_than(p):
        'condition : INSTANCES LESS_THAN NUMBER'
        p[0] = (instances < p[3])

    def p_condition_instances_greater_than(p):
        'condition : INSTANCES GREATER_THAN NUMBER'
        p[0] = (instances > p[3])

    def p_condition_servers_equal(p):
        'condition : SERVERS EQUAL NUMBER'
        p[0] = (servers == p[3])

    def p_condition_servers_not_equal(p):
        'condition : SERVERS NOT_EQUAL NUMBER'
        p[0] = (servers != p[3])

    def p_condition_servers_less_than(p):
        'condition : SERVERS LESS_THAN NUMBER'
        p[0] = (servers < p[3])

    def p_condition_servers_greater_than(p):
        'condition : SERVERS GREATER_THAN NUMBER'
        p[0] = (servers > p[3])

    def p_condition_redundancy_equal(p):
        'condition : REDUNDANCY EQUAL NUMBER'
        p[0] = (redundancy == p[3])

    def p_condition_redundancy_not_equal(p):
        'condition : REDUNDANCY NOT_EQUAL NUMBER'
        p[0] = (redundancy != p[3])

    def p_condition_redundancy_less_than(p):
        'condition : REDUNDANCY LESS_THAN NUMBER'
        p[0] = (redundancy < p[3])

    def p_condition_redundancy_greater_than(p):
        'condition : REDUNDANCY GREATER_THAN NUMBER'
        p[0] = (redundancy > p[3])

    def p_condition_build_equal_assert(p):
        'condition : BUILD EQUAL ASSERT'
        p[0] = (build == p[3])

    def p_condition_build_not_equal_assert(p):
        'condition : BUILD NOT_EQUAL ASSERT'
        p[0] = (build != p[3])

    def p_condition_build_equal_relwithdebinfo(p):
        'condition : BUILD EQUAL RELWITHDEBINFO'
        p[0] = (build == p[3])

    def p_condition_build_not_equal_relwithdebinfo(p):
        'condition : BUILD NOT_EQUAL RELWITHDEBINFO'
        p[0] = (build != p[3])

    def p_condition_build_equal_debug(p):
        'condition : BUILD EQUAL DEBUG'
        p[0] = (build == p[3])

    def p_condition_build_not_equal_debug(p):
        'condition : BUILD NOT_EQUAL DEBUG'
        p[0] = (build != p[3])

    def p_condition_build_equal_profile(p):
        'condition : BUILD EQUAL PROFILE'
        p[0] = (build == p[3])

    def p_condition_build_not_equal_profile(p):
        'condition : BUILD NOT_EQUAL PROFILE'
        p[0] = (build != p[3])

    def p_condition_build_equal_release(p):
        'condition : BUILD EQUAL RELEASE'
        p[0] = (build == p[3])

    def p_condition_build_not_equal_release(p):
        'condition : BUILD NOT_EQUAL RELEASE'
        p[0] = (build != p[3])

    def p_condition_build_equal_valgrind(p):
        'condition : BUILD EQUAL VALGRIND'
        p[0] = (build == p[3])

    def p_condition_build_not_equal_valgrind(p):
        'condition : BUILD NOT_EQUAL VALGRIND'
        p[0] = (build != p[3])

    def p_condition_security_equal_trust(p):
        'condition : SECURITY EQUAL TRUST'
        p[0] = (security == p[3])

    def p_condition_security_not_equal_trust(p):
        'condition : SECURITY NOT_EQUAL TRUST'
        p[0] = (security != p[3])

    def p_condition_security_equal_password(p):
        'condition : SECURITY EQUAL PASSWORD'
        p[0] = (security == p[3])

    def p_condition_security_not_equal_password(p):
        'condition : SECURITY NOT_EQUAL PASSWORD'
        p[0] = (security != p[3])

    def p_condition_security_equal_pam(p):
        'condition : SECURITY EQUAL PAM'
        p[0] = (security == p[3])

    def p_condition_security_not_equal_pam(p):
        'condition : SECURITY NOT_EQUAL PAM'
        p[0] = (security != p[3])

    def p_condition_x_dflt_dist_root_equal(p):
        'condition : XDFLTDISTROOT EQUAL NUMBER'
        p[0] = (x_dflt_dist_root == p[3])

    def p_condition_x_dflt_dist_root_not_equal(p):
        'condition : XDFLTDISTROOT NOT_EQUAL NUMBER'
        p[0] = (x_dflt_dist_root != p[3])

    def p_condition_x_dflt_dist_input_equal(p):
        'condition : XDFLTDISTINPUT EQUAL NUMBER'
        p[0] = (x_dflt_dist_input == p[3])

    def p_condition_x_dflt_dist_input_not_equal(p):
        'condition : XDFLTDISTINPUT NOT_EQUAL NUMBER'
        p[0] = (x_dflt_dist_input != p[3])

    def p_condition_x_dflt_dist_other_equal(p):
        'condition : XDFLTDISTOTHER EQUAL NUMBER'
        p[0] = (x_dflt_dist_other == p[3])

    def p_condition_x_dflt_dist_other_not_equal(p):
        'condition : XDFLTDISTOTHER NOT_EQUAL NUMBER'
        p[0] = (x_dflt_dist_other != p[3])

    # Error rule for syntax errors
    def p_error(p):
        if p:
            error_messages.append(
                "Syntax error on line %d of token type '%s'."
                % (lexer.lineno, p.type))
            # Just discard the token and tell the parser it's okay.
            parser.errok()
        else:
            error_messages.append(
                "Syntax error at EOF on line %d."
                % lexer.lineno)

    def flake8_assigned_not_used():
        return [tokens, t_COMMA, t_EQUAL, t_NOT_EQUAL, t_LESS_THAN,
                t_GREATER_THAN, t_ignore, start]

    # Build the lexer
    lexer = lex.lex(debug=0)

    # Build the parser
    parser = yacc.yacc(debug=0, outputdir='/tmp')

    results = []

    # Process config file if one is given
    # but if a commandline argument was set
    # do NOT override it with the config file.
    if config_file:
        config = RawConfigParser()
        try:
            config.readfp(open(config_file, 'r'))
        except IOError:
            raise AppError("Configuration file: " + config_file +
                           " not found or is not readable.")
        except Exception:
            raise AppError("Can not process a configuration file"
                           " with no sections.")
        if len(config.sections()) > 1:
            raise AppError("Can not process a configuration file"
                           " with more than one section.")
        section_name = config.sections()[0]
        # security
        if security is None:
            if config.has_option(section_name, 'security'):
                security = config.get(section_name, 'security')
            else:
                security = 'trust'
        # redundancy
        if redundancy is None:
            if config.has_option(section_name, 'redundancy'):
                redundancy = int(config.get(section_name, 'redundancy'))
            else:
                redundancy = 1
        # Count servers and instances
        count_servers = 0
        count_instances = 0
        for (key, value) in config.items(section_name):
            if key.startswith('server-'):
                # format: server-N=ip(,n)|(,m-p) [,q-s] number of local workers
                count_servers += 1
                # count instances
                _, instance_list = parse_server_entry(value)
                count_instances += len(instance_list)
        if servers is None:
            servers = count_servers
        if instances is None:
            instances = count_instances
        # x_dflt_dist_root
        if x_dflt_dist_root is None:
            if config.has_option(section_name, 'x-dflt-dist-root'):
                x_dflt_dist_root = int(
                    config.get(section_name, 'x-dflt-dist-root'))
            else:
                x_dflt_dist_root = 1
        # x_dflt_dist_input
        if x_dflt_dist_input is None:
            if config.has_option(section_name, 'x-dflt-dist-input'):
                x_dflt_dist_input = int(
                    config.get(section_name, 'x-dflt-dist-input'))
            else:
                x_dflt_dist_input = 1
        # x_dflt_dist_other
        if x_dflt_dist_other is None:
            if config.has_option(section_name, 'x-dflt-dist-other'):
                x_dflt_dist_other = int(
                    config.get(section_name, 'x-dflt-dist-other'))
            else:
                x_dflt_dist_other = 1
    # Check that all parameters have been defined and are reasonable
    error_messages = []
    if disable_file is None:
        error_messages.append("disable_file has not been set")
    if instances is None:
        error_messages.append("instances has not been set")
    else:
        instances = int(instances)
        if instances not in range(1, 1024):
            error_messages.append("instances=%s is not an acceptable number"
                                  % instances)
    if servers is None:
        error_messages.append("servers has not been set")
    else:
        servers = int(servers)
        if servers not in range(1, 1024):
            error_messages.append("servers=%s is not an acceptable number"
                                  % servers)
    if redundancy is None:
        error_messages.append("redundancy has not been set")
    else:
        redundancy = int(redundancy)
        if redundancy not in range(0, 1024):
            error_messages.append("redundancy=%s is not an acceptable number"
                                  % redundancy)
    if build is None:
        error_messages.append("build has not been set")
    else:
        if build not in build_values:
            error_messages.append("build=%s is not a recognized value"
                                  % build)
    if security is None:
        error_messages.append("security has not been set")
    else:
        if security not in security_values:
            error_messages.append("security=%s is not a recognized value"
                                  % security)

    # x_dftl_dist_root
    if x_dflt_dist_root is None:
        error_messages.append("x_dflt_dist_root has not been set")
    else:
        x_dflt_dist_root = int(x_dflt_dist_root)
        if x_dflt_dist_root not in range(0, 10):
            error_messages.append(
                "x_dflt_dist_root=%s is not an acceptable number"
                % x_dflt_dist_root)

    # x_dftl_dist_input
    if x_dflt_dist_input is None:
        error_messages.append("x_dflt_dist_input has not been set")
    else:
        x_dflt_dist_input = int(x_dflt_dist_input)
        if x_dflt_dist_input not in range(0, 10):
            error_messages.append(
                "x_dflt_dist_input=%s is not an acceptable number"
                % x_dflt_dist_input)

    # x_dftl_dist_other
    if x_dflt_dist_other is None:
        error_messages.append("x_dflt_dist_other has not been set")
    else:
        x_dflt_dist_other = int(x_dflt_dist_other)
        if x_dflt_dist_other not in range(0, 10):
            error_messages.append(
                "x_dflt_dist_other=%s is not an acceptable number"
                % x_dflt_dist_other)

    # Print error messages and exit
    if error_messages:
        raise AppError(error_messages)

    # Process the disable tests file
    with open(disable_file) as fp:
        for line in fp:
            result = parser.parse(line, lexer=lexer, debug=debug)
            if result:
                results.append(result)

    # Output the results
    if new_disable_file != "-":
        with open(new_disable_file, 'w') as fp:
            for res in results:
                fp.write(res + '\n')
    else:
        for res in results:
            print(res)

    return results


def main(argv=None):
    """Argument parsing and last-ditch exception handling.
    """
    if argv is None:
        argv = sys.argv

    global _pgm
    _pgm = "%s:" % os.path.basename(argv[0])  # colon for easy use by print

    parser = argparse.ArgumentParser(
        prog=_pgm,
        description="""Parses a disable.tests file
        and outputs a tests2diable file"""
    )

    parser.add_argument('input',
                        help="Input file (the disable file)")
    parser.add_argument('output',
                        help="Output file or '-' for stdout")
    parser.add_argument('--config_file', '-c',
                        help="Optional configuration file to parse")
    parser.add_argument('--instances', '-i',
                        help="Number of instances")
    parser.add_argument('--servers', '-s',
                        help="Number of servers")
    parser.add_argument('--redundancy', '-r',
                        help="Redundancy level")
    parser.add_argument('--build', '-b',
                        help="Build Type")
    parser.add_argument('--security', '-x',
                        help="Security")
    parser.add_argument('--x-dflt-dist-root',
                        help="Default distribution for query root")
    parser.add_argument('--x-dflt-dist-input',
                        help="Default distribution for input")
    parser.add_argument('--x-dflt-dist-other',
                        help="Default distribution other cases")
    parser.add_argument('--debug', action='store_true',
                        help="Turn on parser debugging")
    parser.add_argument('--syntax', dest='syntax', action='store_true',
                        help="""Show the syntax of a disable.tests file
                        and exit""")
    global _args
    _args = parser.parse_args(argv[1:])

    try:
        parse_disable_file(
            disable_file=_args.input,
            new_disable_file=_args.output,
            config_file=_args.config_file,
            instances=_args.instances,
            servers=_args.servers,
            redundancy=_args.redundancy,
            build=_args.build,
            security=_args.security,
            x_dflt_dist_root=_args.x_dflt_dist_root,
            x_dflt_dist_input=_args.x_dflt_dist_input,
            x_dflt_dist_other=_args.x_dflt_dist_other,
            debug=_args.debug)
        return 0
    except AppError as e:
        print >>sys.stderr, _pgm, e
        return 1
    except Exception as e:
        print >>sys.stderr, _pgm, "Unhandled exception:", e
        traceback.print_exc()   # always want this for unexpected exceptions
        return 2


if __name__ == '__main__':
    sys.exit(main())
