## ISL CMake port
This is a CMake port of the Integer Set Library (work in progress)

isl is a thread-safe C library for manipulating sets and relations
of integer points bounded by affine constraints.  The descriptions of
the sets and relations may involve both parameters and existentially
quantified variables.  All computations are performed in exact integer
arithmetic using GMP.

isl is released under the MIT license, but depends on the LGPL GMP
library.

Minimal compilation instructions:

	./configure
	make
	make install

If you are taking the source from the git repository, then you first
need to do

	git clone git://repo.or.cz/isl.git
	./autogen.sh

For more information, see doc/user.pod or the generated documentation.

New releases are announced on http://groups.google.com/group/isl-announce

If you use isl, you can let me know by stacking
https://www.openhub.net/p/isl on Open Hub.

For bug reports, feature requests and questions,
contact http://groups.google.com/group/isl-development

Whenever you report a bug, please mention the exact version of isl
that you are using (output of "./isl_cat --version").  If you are unable
to compile isl, then report the git version (output of "git describe")
or the version included in the name of the tarball.

If you use isl for your research, you are invited do cite
the following paper and/or the paper(s) describing the specific
operations you use.

@incollection{Verdoolaege2010isl,
   author = {Verdoolaege, Sven},
   title = {isl: An Integer Set Library for the Polyhedral Model},
   booktitle = {Mathematical Software - ICMS 2010},
   series = {Lecture Notes in Computer Science},
   editor = {Fukuda, Komei and Hoeven, Joris and Joswig, Michael and
		Takayama, Nobuki},
   publisher = {Springer},
   isbn = {978-3-642-15581-9},
   pages = {299-302},
   volume = {6327},
   year = {2010}
}
