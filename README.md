What is Sparkling?
==================
Sparkling is a little C-style scripting language I've started as a pet project
back in late 2012. It has evolved into a quite serious code base now, so I'm
opensourcing it in the hope that 1. it will be useful for the community, and
2. others seeing the potential in it will help me make it better.

On the one hand, the name "Sparkling" comes from my intent to make the language
nice, fast and lightweight, properties I associate with sparks in my mind.
On the other hand, my nickname (H2CO3) has a lot to do with carbonated water
and bubbles.

Sparkling is influenced by other programming languages, namely:

 - **C, above all.** C is a wonderful programming language, it's well-balanced
   between a high-level and a low-level language. It has enough but not too
   much abstraction. Its syntax is clear, concise and well-structured. Quite a
   lot of languages inherit some of C's design patterns, especially syntax.
 - **Lua.** Being small, compact, fast and embeddable was a primary goal while
   I have been designing Sparkling. In addition, having a separate operator
   for concatenation is *just inevitable.* Furthermore, making a strict
   distinction between arrays and dictionaries is something I always wanted to
   avoid, and Lua confirmed that it's a viable concept in production.
 - **Python.** Python has dynamic *and* strong typing. That's good because it's
   convenient and safe at the same time. Following this pattern, Sparkling has
   a strict strong type sytem, but variables don't have types, only values do,
   so any value can be stored in a variable, and runtime checks enforce the
   correctness of operations.
 - **Design errors of JavaScript.** JavaScript is a horrible language in my
   opinion. (Sorry, but I feel like that, nobody has to agree.) It has some
   fundamental flaws that I wanted to eliminate in order to create a sane
   language. Overloading the '+' operator for concatenation, equality operators
   that don't work the way one would expect, block statements without scope,
   implicitly global undeclared variables, semi-reserved (unused and unusable)
   keywords, object and function names hard-coded into the language -- argh.
   And at the end, people even call JavaScript a "small and lightweight"
   language, even though when we look at any decent JavaScript engine (just
   think of V8, Nitro, SquirrelFish, SpiderMonkey, etc.), all of them consist
   of dozens or even hundreds of megabytes of sophisticated, overly complex
   code. There isn't any need for that in an embedded scripting language.
   The only thing that Sparkling borrows from JavaScript is the keyword for
   declaring variables: `var`.
 - **Other "default" mistakes** that almost every scripting language has had so
   far. For example, the exclusive use of floating-point numbers for arithmetic
   operations, even for integers. It's a well-known fact that floating-point
   computations aren't always exact, they behave in a quite counter-intuitive
   manner and they can even be significantly slower than integer operations.
   Another thing is *garbage collection.* It seems to be the silver bullet of
   automatic memory management when it comes to scripting languages, but it
   has some definitive, serious downsides. Non-deterministic behavior is one,
   speed is two, memory overhead is three, the difficulty of a decent
   implementation is four. Reference counting is a simpler approach that is
   more lightweight, easier to implement, completely deterministic and it has
   no memory allocation overhead (a program uses only as much memory as needed).
   Refcounting has its disadvantages too (e. g. memory leaks caused by cyclic
   references), but I believe that its good properties outweigh them.

Why Sparkling?
==============
- Small, embeddable: compiled size less than 100kB; tiny grammar, simple rules
- Portable: written in pure ANSI C89 - runs on Windows, Linux, OS X, iOS, ...
   it can also be used from within a C++ program (apart from the requirement
   that the library itself should be compiled as C and not as C++, the public
   parts of the API are entirely C++-compatible.)
- Well-designed (at least it is supposed to be well-designed):
  - strict yet dynamic typing and consistent semantics: less programmer errors
  - uses integers to perform integer operations (faster, exact, allows bit ops)
  - easy-to-grasp, readable C-stlye syntax
  - no global variables
  - no implicit conversions or type coercion
- Fast: speed comparable to that of Lua
- Friendly: automatic memory management
- Extensible: simple and flexible C API
- Free: Sparkling is free software, the reference implementation is licensed
  under the 2-clause BSD License (see LICENSE.txt for details).

How do I use it?
================
To learn Sparkling, check out the tutorial/reference manual in `doc/`,
then have a look at the examples in the `examples/` directory. Don't worry,
there will be more and more documentation over time, but for now that's all
I've got.

Using the Sparkling engine is fairly easy. As in the case of practically any
modern scripting language, running a program involves three simple steps:

  1. Parse the source text;
  2. Compile it into bytecode;
  3. Execute the bytecode.

The Sparkling C API provides functions for these tasks. For usage information,
see `examples/basic.c`. You may also want to have a look at implementation
of the stand-alone interpreter, located in `repl.c`.

How do I hack on it?
====================
If you have fixed a bug, improved an algorithm or otherwise contributed to the
library and you feel like sharing your work with the community, please send me
a pull request on GitHub with a concise description of the changes. I only ask
you to please respect and follow my coding style (placement of brackets and
asterisks, indentation and alignment, whitespace, comments, etc.)

The Sparkling API also has some very basic debugging facilities: namely, it is
possible to dump the abstract syntax tree of a parsed program (in order to
examine the behavior of the parser) and one can disassemble compiled bytecode
as well (so as to debug the compiler and the virtual machine).

What else?
==========
If you have any questions or suggestions, you have used Sparkling in your
project or you want to share anything else with me, feel free to drop me an
e-mail or a tweet (I run by the name [@H2CO3_iOS](http://twitter.com/H2CO3_iOS)
on Twitter). You may also find me on [irc.freenode.net](http://irc.freenode.net)
by the same nick, on the channel `#sparkling`. I appreciate any feedback,
including constructive (and polite) criticism, improvement suggestions,
questions about usage (if the documentation is unclear), and even donations :P

If you are using Gedit
for coding, check out the `sparkling.lang` file -- install it in the appropriate
location to have Gedit recognize the syntax of Sparkling and apply syntax
highlighting on your code.

This is an alpha release, so don't expect the engine to be perfect (actually,
not even feature-complete). Although I always try to do my best and test
my code as much as possible, there may still be bugs - let me know if you find
one and I'll fix it as soon as possible. The syntax and semantics of the
language are subject to change, too (at least until it leaves alpha), so in
the early days, code that ran yesterday can break today. But this is done only
in order to let the community decide what kind of features, syntactic and
semantic rules would be the best, and when I will have gathered enough
suggestions, I'll freeze the language specification. (This is also good for me
since now I can procrastinate writing the full specs until the beta release...)

In the meantime, please experiment with the library, write extensions, **try
to break the code** (I appreciate bug reports), play around with the engine in
various situations, on different platforms. The more people use Sparkling,
the better it will become. Check out the platform-specific Makefiles (with
special regards to the `BUILD` variable) as well and tailor them to your needs.

Happy programming!

-- H2CO3

