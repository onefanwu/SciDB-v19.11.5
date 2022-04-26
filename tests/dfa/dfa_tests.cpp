/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2016-2019 SciDB, Inc.
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
 * @file dfa_test.cpp
 * @author Mike Leibensperger <mjl@paradigm4.com>
 *
 * Unit tests for DFA regular expression recognizer code.
 */

#include <util/DFA.h>

#include <algorithm>            // for find
#include <cassert>
#include <iostream>
#include <string>
#include <string.h>             // for strcmp
#include <vector>

namespace scidb { namespace dfa {
bool debug_flag = false;
} }

using namespace std;

using RE = scidb::dfa::RE<string>;
using Symbol = scidb::dfa::Symbol<string>;

// Shorthand for some terminals used in the test cases.
RE a("a");      RE t("t");      RE input("input");
RE b("b");      RE u("u");      RE array_ref("array_ref");
RE c("c");      RE v("v");      RE attr("attr");
RE d("d");      RE w("w");      RE dim("dim");
RE e("e");      RE x("x");      // something
RE f("f");      RE y("y");      RE expr("expr");
RE g("g");      RE z("z");      RE schema("schema");

enum TestResult
{
    PASS,       // String matches regex.
    FAIL,       // Accepting state not reached, illegal next move: utter fail.
    SHORT,      // String partially matches but does not reach accepting state.
    LONG,       // String arrives at accepting state but has additional tokens.
};

string result(TestResult tr)
{
    switch (tr) {
    case PASS:  return "PASS";
    case FAIL:  return "FAIL";
    case SHORT: return "SHORT";
    case LONG:  return "LONG";
    default:    return "(WTF?)";
    }
}

struct TestCase {
    TestResult  expect;
    const char* test;
};

struct TestGroup {
    const char* name;
    RE regex;
    vector<TestCase> testcases;
};

vector<TestGroup> testgroups = {
    { "Animal noises",

      RE(RE::LIST,
        { input,
          RE(RE::OR, { RE("burp"), RE("meow"), RE("oink"), RE("woof") }),
          RE(RE::PLUS, { a, b }) }),

      { { PASS, "input meow a b" },
        { PASS, "input burp a b a b" },
        { PASS, "input woof a b a b a b" },
        { SHORT, "input" },
        { SHORT, "input oink" },
        { SHORT, "input oink a b a" },
        { FAIL, "input oink t u v" },
        { FAIL, "input v" },
     }
    },

    { "Simple OR",

      RE(RE::OR, { a, b, c }),

      { { PASS, "a" },
        { PASS, "b" },
        { PASS, "c" },
        { FAIL, "d" },
        { FAIL, "( a )" },
        { SHORT, "" },
        { LONG, "a v" },
      }
    },

    { "Simple PLUS",

      RE(RE::PLUS, {a}),

      { { PASS, "a" },
        { PASS, "a a a a a a a a" },
        { SHORT, "" },
        { FAIL, "a a a b" },
      }
    },

    { "PLUS and QMARK",

      RE(RE::LIST,
       { input, RE(RE::PLUS, {a, b}), RE(RE::QMARK, {c})}),

      { { PASS, "input a b" },
        { PASS, "input a b c" },
        { PASS, "input a b a b a b a b c" },
        { FAIL, "input a b a b a b a c" },
        { SHORT, "input a" },
        { SHORT, "input" },
        { SHORT, "" },
        { LONG, "input a b a b a b a b c a" },
      }
    },

    { "STAR, PLUS, QMARK",

      RE(RE::LIST,
       { x,
         RE(RE::PLUS, {a, b}),
         RE(RE::STAR, {c, d}),
         RE(RE::QMARK, {c}) }),

      { { PASS, "x a b" },
        { PASS, "x a b a b a b" },
        { PASS, "x a b a b c d" },
        { PASS, "x a b a b c d c d c d c d" },
        { PASS, "x a b a b c" },
        { PASS, "x a b c d c" },
        { FAIL, "x a c d c" },
        { FAIL, "x a b a c d c" },
        { FAIL, "x a b a b c d c d c d c d x" },
        { SHORT, "x" },
        { SHORT, "x a" },
        { FAIL, "x a x" },
      }
    },

    { "Subgroups",

      RE(RE::LIST,
       { w,
         RE(RE::GROUP, { a, b }),
         x,
         RE(RE::GROUP, { c, d }),
         y }),

      { { PASS,  "w ( a b ) x ( c d ) y" },
        { SHORT, "w" },
        { SHORT, "w (" },
        { SHORT, "w ( a" },
        { SHORT, "w ( a b" },
        { SHORT, "w ( a b )" },
        { SHORT, "w ( a b ) x" },
        { SHORT, "w ( a b ) x (" },
        { SHORT, "w ( a b ) x ( c" },
        { SHORT, "w ( a b ) x ( c d" },
        { SHORT, "w ( a b ) x ( c d )" },
        { LONG,  "w ( a b ) x ( c d ) y z" },
      }
    },

    { "input() operator",

      RE(RE::LIST,
       { schema,
         RE("filename"),
         RE(RE::QMARK,
          { RE(RE::OR,
             { RE("instance"),
               RE(RE::LIST, { RE("instance"), RE("format") }),
               RE(RE::LIST, { RE("instance"), RE("format"), RE("maxErr") }),
               RE(RE::LIST, { RE("instance"), RE("format"), RE("maxErr"),
                              RE("strict") })
             })
          })
       }),

      // Same tests as next group!
      { { PASS, "schema filename" },
        { PASS, "schema filename instance" },
        { PASS, "schema filename instance format" },
        { PASS, "schema filename instance format maxErr" },
        { PASS, "schema filename instance format maxErr strict" },
        { SHORT, "" },
        { SHORT, "schema" },
        { FAIL, "schema filename x" },
        { FAIL, "schema filename instance x" },
        { FAIL, "schema filename instance format x" },
        { FAIL, "schema filename instance format maxErr x" },
        { LONG, "schema filename instance format maxErr strict x" },
      }
    },

    { "input() operator, expressed with RE::EMPTY",

      RE(RE::LIST,
       { schema,
         RE("filename"),
         RE(RE::OR,
          { RE(RE::EMPTY),
            RE("instance"),
            RE(RE::LIST, { RE("instance"), RE("format") }),
            RE(RE::LIST, { RE("instance"), RE("format"), RE("maxErr") }),
            RE(RE::LIST, { RE("instance"), RE("format"), RE("maxErr"),
                           RE("strict") })
          })
       }),

      // Same tests as previous group!
      { { PASS, "schema filename" },
        { PASS, "schema filename instance" },
        { PASS, "schema filename instance format" },
        { PASS, "schema filename instance format maxErr" },
        { PASS, "schema filename instance format maxErr strict" },
        { SHORT, "" },
        { SHORT, "schema" },
        { FAIL, "schema filename x" },
        { FAIL, "schema filename instance x" },
        { FAIL, "schema filename instance format x" },
        { FAIL, "schema filename instance format maxErr x" },
        { LONG, "schema filename instance format maxErr strict x" },
      }
    },

    { "apply() operator",

      RE(RE::LIST, {input, RE(RE::PLUS, {attr, expr})}),

      { { PASS, "input attr expr" },
        { PASS, "input attr expr attr expr" },
        { PASS, "input attr expr attr expr attr expr" },
        { PASS, "input attr expr attr expr attr expr attr expr" },
        { SHORT, "input attr expr attr expr attr expr attr" },
        { SHORT, "input" },
        { FAIL, "x attr expr attr expr attr expr" },
        { FAIL, "input x expr attr expr attr expr" },
        { FAIL, "input attr x attr expr attr expr" },
        { FAIL, "input attr expr x expr attr expr" },
        { FAIL, "input attr expr attr x attr expr" },
        { FAIL, "input attr expr x" },
        { FAIL, "input attr expr attr expr x" },
      }
    },

    { "apply() operator with nested parameter lists",

      RE(RE::LIST, {input, RE(RE::PLUS, { RE(RE::GROUP, {attr, expr}) }) }),

      { { PASS, "input ( attr expr )" },
        { PASS, "input ( attr expr ) ( attr expr )" },
        { PASS, "input ( attr expr ) ( attr expr ) ( attr expr )" },
        { PASS, "input ( attr expr ) ( attr expr ) ( attr expr ) "
                "( attr expr )" },
        { SHORT, "input ( attr expr ) ( attr expr ) ( attr expr ) ( attr" },
        { SHORT,"input ( attr expr ) ( attr expr ) ( attr expr ) ( attr expr" },
        { SHORT, "input" },

        // These passed in the previous group, but not now.
        { FAIL, "input attr expr" },
        { FAIL, "input attr expr attr expr" },
        { FAIL, "input attr expr attr expr attr expr" },
        { FAIL, "input attr expr attr expr attr expr attr expr" },

        { FAIL, "x ( attr expr ) ( attr expr ) ( attr expr )" },
        { FAIL, "input ( x expr ) ( attr expr ) ( attr expr )" },
        { FAIL, "input ( attr expr attr expr ) (  attr expr )" },
      }
    },

    { "apply() operator with nested parameter lists and backward compatibility",

        RE(RE::LIST,
          { input,
            RE(RE::OR,
             { RE(RE::PLUS, { attr, expr }),
               RE(RE::PLUS,
                { RE(RE::GROUP, { attr, expr }) }) }) }),

      {  // New syntax
        { PASS, "input ( attr expr )" },
        { PASS, "input ( attr expr ) ( attr expr )" },
        { PASS, "input ( attr expr ) ( attr expr ) ( attr expr )" },
        { PASS,"input ( attr expr ) ( attr expr ) ( attr expr ) ( attr expr )"},
        { SHORT, "input ( attr expr ) ( attr expr ) ( attr expr ) ( attr" },
        { SHORT,"input ( attr expr ) ( attr expr ) ( attr expr ) ( attr expr" },
        { SHORT, "input" },
        { FAIL, "x ( attr expr ) ( attr expr ) ( attr expr )" },
        { FAIL, "input ( x expr ) ( attr expr ) ( attr expr )" },
        { FAIL, "input ( attr expr attr expr ) (  attr expr )" },

        // Old syntax
        { PASS, "input attr expr" },
        { PASS, "input attr expr attr expr" },
        { PASS, "input attr expr attr expr attr expr" },
        { PASS, "input attr expr attr expr attr expr attr expr" },
        { SHORT, "input attr expr attr expr attr expr attr" },
        { SHORT, "input" },
        { FAIL, "x attr expr attr expr attr expr" },
        { FAIL, "input x expr attr expr attr expr" },
        { FAIL, "input attr x attr expr attr expr" },
        { FAIL, "input attr expr x expr attr expr" },
        { FAIL, "input attr expr attr x attr expr" },
        { FAIL, "input attr expr x" },
        { FAIL, "input attr expr attr expr x" },

        // Mixed syntax fails.
        { FAIL, "input attr expr ( attr expr )" },
        { FAIL, "input ( attr expr ) attr expr" },
     }
    },

    { "avg_rank() operator",

      RE(RE::LIST, { input,
                     RE(RE::OR, {
                         RE(RE::EMPTY),
                         RE(RE::LIST, { attr, RE(RE::STAR, {dim}) })
                     })
                  }),

      { { PASS, "input" },
        { PASS, "input attr" },
        { PASS, "input attr dim" },
        { PASS, "input attr dim dim" },
        { PASS, "input attr dim dim dim" },
        { PASS, "input attr dim dim dim dim" },
        { FAIL, "input attr attr dim dim dim" },
      }
    },

    { "Possible new syntax for between() and subarray()",

      RE(RE::LIST,
       { input,
         RE(RE::PLUS, { RE(RE::GROUP, { dim, RE("low"), RE("high") })})}),

      { { PASS, "input ( dim low high )" },
        { PASS, "input ( dim low high ) ( dim low high )" },
        { PASS, "input ( dim low high ) ( dim low high ) ( dim low high )" },
        { FAIL, "input ( dim low ) ( dim low high ) ( dim low high )" },
        { FAIL, "input ( dim low high ) ( dim high ) ( dim low high )" },
        { SHORT, "input" },
        { FAIL, "input low low low high high high" },
        { FAIL,"input ( dim low high ) ( dim low high ) ( dim low high ) dim" },
      }
    },

    { "Possible new syntax for xgrid()",

      RE(RE::LIST,
       { input,
         RE(RE::PLUS, { RE(RE::GROUP, { dim, RE("scale_factor") })})}),

      { { PASS, "input ( dim scale_factor )" },
        { PASS, "input ( dim scale_factor ) ( dim scale_factor )" },
        { SHORT, "input" },
        { FAIL, "input ( dim scale_factor dim scale_factor )" },
        { FAIL, "input ( dim ) ( scale_factor ) ( dim scale_factor )" },
      }
    },

    { "Forthcoming new syntax for cast()",

      RE(RE::LIST,
       { input,
         RE(RE::PLUS,
          { RE(RE::OR,
             { schema,
               array_ref,
               RE(RE::GROUP, { RE("old"), RE("new") }) }) }) }),

      { { PASS, "input schema" },
        { PASS, "input array_ref" },
        { PASS, "input ( old new )" },
        { PASS, "input ( old new ) array_ref" },
        { PASS, "input array_ref ( old new ) array_ref" },
        { PASS, "input array_ref ( old new ) schema schema array_ref "
                "( old new )" },
        { SHORT, "input" },
        { FAIL, "input ( old )" },
        { FAIL, "input ( old schema )" },
        { SHORT, "input ( old new" },
        { FAIL, "input old new )" },
      }
    },

    { "Possible new syntax for slice()",

      RE(RE::LIST,
       { input,
         RE(RE::PLUS,
          { RE(RE::GROUP,
               { dim, RE("value") }) }) }),

      { { PASS, "input ( dim value )" },
        { PASS, "input ( dim value ) ( dim value )" },
        { SHORT, "input" },
        { FAIL, "input ( dim )" },
        { FAIL, "input ( value )" },
        { FAIL, "input ( dim value dim value )" },
        { FAIL, "input ( dim value ) ( dim value ) dim value" },
      }
    },

    { "Buggy syntax for _append_helper()",
      // Translator is greedy and considers array_refs as inputs, so
      // this syntax is ambiguous in SciDB (though it works here).

      RE(RE::LIST,
       { input,
         RE(RE::QMARK, { input }),
         array_ref,
         RE(RE::QMARK, { dim }) }),

      { { PASS, "input array_ref" },
        { PASS, "input input array_ref" },
        { FAIL, "input input input array_ref" },
        { PASS, "input array_ref dim" },
        { FAIL, "input array_ref array_ref dim" },
        { LONG, "input array_ref dim dim" },
        { PASS, "input input array_ref dim" },
        { SHORT, "input" },
      }
    },

};


#define lose1(_reason)                          \
do {                                            \
    cout << "FAILED\n" << _reason << endl;      \
} while(0)


void lose(string const& reason, vector<string> const& moves, size_t move_idx)
{
    assert(move_idx < moves.size());
    lose1(reason);

    string cmd;
    for (size_t i = 0; i < move_idx; ++i) {
        if (i) {
            cmd += ' ';
        }
        cmd += moves[i];
    }

    string marker((move_idx ? cmd.size() + 1 : 0), ' ');
    marker += string(moves[move_idx].size(), '^');

    for (size_t i = move_idx; i < moves.size(); ++i) {
        if (i) {
            cmd += ' ';
        }
        cmd += moves[i];
    }
    cout << '\n' << cmd << '\n' << marker << endl;
}


Symbol make_symbol(string const& s)
{
    Symbol sym(s);
    if (s == "(") {
        sym = Symbol::makePush();
    } else if (s == ")") {
        sym = Symbol::makePop();
    }
    return sym;
}


int main(int ac, char** av)
{
    if (ac >= 2 && !::strcmp(av[1], "-v")) {
        scidb::dfa::debug_flag = true;
    }

    scidb::dfa::NFA<string> nfa;
    scidb::dfa::DFA<string> dfa;

    int losses = 0;
    int ntests = 0;

    struct next_test {};

    for (auto const& group : testgroups) {

        cout << "----\nGroup: " << group.name
             << "\nRegex: " << group.regex.asRegex() << endl;

        nfa.compile(group.regex);
        dfa.build(nfa);

        for (auto const& tc : group.testcases) {
            cout << ntests++ << ":\t\"" << tc.test << "\" ... " << flush;

            // Break testcase string up into move tokens.
            istringstream iss(tc.test);
            vector<string> moves;
            string s;
            while (iss >> s) {
                moves.push_back(s);
            }

            try {

                // Walk the state machine!
                auto pos = dfa.getCursor();
                for (size_t i = 0; i < moves.size(); ++i) {

                    vector<Symbol> expected(pos.getExpected());
                    if (expected.empty()) {
                        if (tc.expect != LONG) {
                            lose("Too many moves!", moves, i);
                            ++losses;
                        } else {
                            cout << "OK (long)" << endl;
                        }
                        throw next_test();
                    }

                    Symbol sym = make_symbol(moves[i]);

                    auto m = find(expected.begin(), expected.end(), sym);
                    if (m == expected.end()) {
                        if (tc.expect != FAIL) {
                            lose("Illegal move!", moves, i);
                            ++losses;
                        } else {
                            cout << "OK (fail)" << endl;
                        }
                        throw next_test();
                    }

                    pos.move(sym);
                    assert(!pos.isError()); // ...else "expected" check was bad.
                }

                // All moves were good, but are we in an accepting state?
                if (pos.isAccepting()) {
                    if (tc.expect == PASS) {
                        cout << "OK (pass)" << endl;
                    } else {
                        lose1("Test passed but was expecting "
                              << result(tc.expect) << " failure");
                        ++losses;
                    }
                } else if (tc.expect == SHORT) {
                    cout << "OK (short)" << endl;
                } else {
                    lose("Not enough moves to reach an accepting state!",
                         moves, moves.size() - 1);
                    ++losses;
                }
            }
            catch (next_test&) {
                // Onward!
            }
        }
    }

    cout << '\n';
    if (losses) {
        cout << losses << " of " << ntests << " tests failed" << endl;
    } else {
        cout << "All " << ntests << " tests passed" << endl;
    }
    return losses;
}
