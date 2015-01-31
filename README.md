partitioned_elias_fano
======================

NOTE: This repository is maintained only for historical reasons. This code is
now part of [ds2i](https://github.com/ot/ds2i).

This repository contains the code used for the experiments in the paper

* Giuseppe Ottaviano and Rossano Venturini, _Partitioned Elias-Fano Indexes_,
  ACM SIGIR 2014.


Building the code
-----------------

The code is tested on Linux with GCC 4.9 and OSX Mavericks with Clang.

The following dependencies are needed for the build.

* CMake >= 2.8, for the build system
* Boost >= 1.42

The code depends on several git submodules. If you have cloned the repository
without `--recursive`, you will need to perform the following commands before
building:

    $ git submodule init
    $ git submodule update

To build, it should be sufficient to do:

    $ cmake . -DCMAKE_BUILD_TYPE=Release
    $ make

It is also preferable to perform a `make test`, which runs the unit tests.


Running the experiments
-----------------------

The directory `test/test_data` contains a small document collection used in the
unit tests. The binary format of the collection is described in the next
section.

To create an index use the command `create_freq_index`. The available index
types are listed in `index_types.hpp`. For example, to create an index using the
optimal partitioning algorithm using the test collection, execute the command:

    $ ./create_freq_index opt test/test_data/test_collection test_collection.index.opt --check

where `test/test_data/test_collection` is the _basename_ of the collection, that
is the name without the `.{docs,freqs,sizes}` extensions, and
`test_collection.index.opt` is the filename of the output index. `--check`
perform a verification step to check the correctness of the index.

To perform BM25 queries it is necessary to build an additional file containing
the parameters needed to compute the score, such as the document lengths. The
file can be built with the following command:

    $ ./create_wand_data test/test_data/test_collection test_collection.wand

Now it is possible to query the index. The command `queries` parses each line of
the standard input as a tab-separated collection of term-ids, where the i-th
term is the i-th list in the input collection. An example set of queries is
again in `test/test_data`.

    $ ./queries opt test_collection.index.opt test_collection.wand < test/test_data/queries


Collection input format
-----------------------

A _binary sequence_ is a sequence of integers prefixed by its length, where both
the sequence integers and the length are written as 32-bit little-endian
unsigned integers.

A _collection_ consists of 3 files, `<basename>.docs`, `<basename>.freqs`,
`<basename>.sizes`.

* `<basename>.docs` starts with a singleton binary sequence where its only
  integer is the number of documents in the collection. It is then followed by
  one binary sequence for each posting list, in order of term-ids. Each posting
  list contains the sequence of document-ids containing the term.

* `basename.freqs` is composed of a one binary sequence per posting list, where
  each sequence contains the occurrence counts of the postings, aligned with the
  previous file (note however that this file does not have an additional
  singleton list at its beginning).

* `basename.sizes` is composed of a single binary sequence whose length is the
  same as the number of documents in the collection, and the i-th element of the
  sequence is the size (number of terms) of the i-th document.


Authors
-------

* Giuseppe Ottaviano <giuott@gmail.com>
* Rossano Venturini <rossano@di.unipi.it>
