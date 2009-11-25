{ use 5.006; }
use warnings;
use strict;

use ExtUtils::MakeMaker;

WriteMakefile(
    NAME => "PHP::Embed",
    VERSION_FROM => "Embed.pm",
    PREREQ_PM => {},
    ABSTRACT_FROM => "Embed.pm",
    OBJECT => qq(php_scanner.o Embed.o),
    DEFINE => '-D_GNU_SOURCE',
    AUTHOR => "Moriyoshi Koizumi <mozo\@mozo.jp>",
);

sub MY::install { "install ::\n" }

sub MY::postamble {
    <<'MAKE_FRAG'
%.c: %.re
	re2c --case-inverted -cbdF -t php_scanner_defs.h $< > $@
MAKE_FRAG
}
1;
