=head1 NAME

PHP::Embed - embed PHP scripts in any Perl script

=head1 SYNOPSIS

	use PHP::Embed;

=head1 DESCRIPTION

    TBD

=cut

package PHP::Embed;

{ use 5.011001; }
use warnings;
use strict;

our $VERSION = "0.003";

require XSLoader;
XSLoader::load(__PACKAGE__, $VERSION);

=head1 BUGS

    You will find no reason to use PHP from within your Perl script like this. 

=head1 SEE ALSO

L<PHP>,
L<PHP::Include>

=head1 AUTHOR

Moriyoshi Koizumi <mozo@mozo.jp>

=head1 COPYRIGHT

Copyright (C) 2009 Moriyoshi Koizumi <mozo@mozo.jp>

=head1 LICENSE

This module is free software; you can redistribute it and/or modify it
under the same terms as Perl itself.

=cut

1;
