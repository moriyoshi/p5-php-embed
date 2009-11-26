use PHP::Embed;

$a = <?php print "
あ
ほ
か";
?>;
print $a, "\n";
