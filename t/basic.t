use warnings;
use strict;
use PHP::Embed;
use Test::More tests => 2;

is(<?php echo "ok"; ?>, "ok");
is(<?php echo "
test " ?>, "\ntest ");
1;
