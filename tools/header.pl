#!/usr/bin/perl

use POSIX 'strftime';

my $infile = $ARGV[0];
my $outfile = $ARGV[1];
my $w = $ARGV[2];
my $h = $ARGV[3];

{
	local $| = 4096;
	local *OUT;
	open(OUT, "> $outfile") or die("could not create a file: $!");
	binmode(OUT);
	my $note = $0 . " " . "@ARGV |" . strftime("%Y/%m/%d %H:%M:%S (%Z)", localtime);
	{
		use bytes;
		$notebytes = bytes::length($note);
	}
	$notebytes = ($notebytes + 15) & (~15);
	print "$note ($notebytes)\n";
	local $_;
	local *IN;
	open(IN, "< $infile") or die("could not open input file: $!");
	binmode(IN);
	{
		local $_;
		print OUT pack("a4vvVVVVVV", "RAWD", 1, 0, $w * 4, $w, $h, 0, 4, $notebytes);
		print OUT pack("a$notebytes", $note);
		print OUT $_ while <IN>;
	}
}
