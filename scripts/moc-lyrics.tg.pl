#!/usr/bin/perl -w
#===============================================================================#{{{
#
#         FILE:  moc-lyrics.pl
#
#        USAGE:  ./moc-lyrics.pl 
#                   -a  --artist ARTIST
#                   -t  --title  TITLE
#
#  DESCRIPTION:  Retrieves lyrics from some sites and saves it to a file
#                To customize its behaviour have a look at the variables under
#                "Definitions"
#
#        NOTES:  Lyrics::Fetcher is free software by David Precious
#                (URL: http://search.cpan.org/~bigpresh/)
#       AUTHOR:  kb, <unixprog@gmail.com>, modified by Tomasz Golinski <tomaszg@alpha.uwb.edu.pl>
#===============================================================================#}}}

use Lyrics::Fetcher;
use Encode;
use Getopt::Std;
use File::Basename;
#use Encode::Guess qw/cp1251 latin2/;

getopt('atf', \%opts);

if (!($opts{'a'} && $opts{'t'} && $opts{'f'})){
    die "Usage: -a artist -t track -f filename\n";}

$artist=$opts{'a'};
$title=$opts{'t'};
($filename,$dir) = fileparse($opts{'f'}, qr/\.[^.]*/);

if (-e $dir.$filename) { die "plik istnieje!\n";}

##################################################
# Definitions
###################################################{{{
our $INDENT  = 4;      # Number of spaces for indentation (if any)
our $HEADING = 1;      # Print a heading?
our $SOURCE  = 1;      # Show source of lyrics?
our $PAGER   = 'less'; # Pager to show lyrics (less,more,most,less.sh,info,cat etc.)
our $TRYHARD = 1;      # Try harder by changing case if nothing is found? (this means
                       # a lot  of bandwith usage, use with care on slow connections)
our $DEBUG   = 1;      # Show debug warnings?

# Comment in/out what you want. order does matter.
# For more check out Lyrics::Fetcher's CPAN page
our @sources = (
    'LyricWiki',
    #'AstraWeb',
    #'AZLyrics',
    #'LyricsNet',
    #'LyricsTime',
    #'LeosLyrics',
    #'LyrDB',
    #'Lyrics007',
    #'LyricsDownload',
    'Google',
);
#}}}

##################################################
# Actual lyrics catching
###################################################{{{
our $lyrics;    # Lyrics as string (if any)
our $source;    # Source of the lyrics (if any)

&get_lyrics( $artist, $title );

# Catch lyrics for every source, until one yields a result
sub get_lyrics {
    my ($artist, $title) = @_;
    foreach my $site (@sources) {
        warn "Trying $site" if $DEBUG;
        $lyrics = Lyrics::Fetcher->fetch( $artist, $title, $site );
        if ($lyrics) {
            $source = $site;
            last;
        }
        warn "Found nothing @ $site" if $DEBUG;
    }
}
#}}}

##################################################
# Heuristic stuff
###################################################{{{

# Try harder
if ($TRYHARD) {
    # try it with every starting letter uppercase
    # (e.g. 'Law of Average' => 'Law Of Average')
    unless ($lyrics) { #title
        my $title_guess = &upcase_first( $title );
        warn "Rewritten: '$title' => '$title_guess'" if $DEBUG;
        &get_lyrics( $artist, $title_guess );
    }
    unless ($lyrics) { #artist
        my $artist_guess = &upcase_first( $artist );
        warn "Rewritten: '$artist' => '$artist_guess'" if $DEBUG;
        &get_lyrics( $artist_guess, $title );
    }
    unless ($lyrics) { #both
        my $artist_guess = &upcase_first( $artist );
        my $title_guess = &upcase_first( $title );
        warn "Rewritten: '$artist' => '$artist_guess'" if $DEBUG;
        warn "Rewritten: '$title' => '$title_guess'" if $DEBUG;
        &get_lyrics( $artist_guess, $title_guess );
    }

    # try it with every starting letter uppercase but making 
    # all other letters lowercase 
    # (e.g. 'LAW oF AVERAGE' => 'Law Of Average')
    unless ($lyrics) { #title
        my $title_guess = &upcase_first( lc($title) );
        warn "Rewritten: '$title' => '$title_guess'" if $DEBUG;
        &get_lyrics( $artist, $title_guess );
    }
    unless ($lyrics) { #artist
        my $artist_guess = &upcase_first( lc($artist) );
        warn "Rewritten: '$artist' => '$artist_guess'" if $DEBUG;
        &get_lyrics( $artist_guess, $title );
    }
    unless ($lyrics) { #both
        my $artist_guess = &upcase_first( lc($artist) );
        my $title_guess = &upcase_first( lc($title) );
        warn "Rewritten: '$artist' => '$artist_guess'" if $DEBUG;
        warn "Rewritten: '$title' => '$title_guess'" if $DEBUG;
        &get_lyrics( $artist_guess, $title_guess );
    }

    # try it with all-caps (i.e. 'ac/dc' => 'AC/DC')
    unless ($lyrics) { #title
        my $title_guess = uc($title);
        warn "Rewritten: '$title' => '$title_guess'" if $DEBUG;
        &get_lyrics( $artist, $title_guess );
    }
    unless ($lyrics) { #artist
        my $artist_guess = uc($artist);
        warn "Rewritten: '$artist' => '$artist_guess'" if $DEBUG;
        &get_lyrics( $artist_guess, $title );
    }
    unless ($lyrics) { #both
        my $artist_guess = uc($artist);
        my $title_guess = uc($title);
        warn "Rewritten: '$artist' => '$artist_guess'" if $DEBUG;
        warn "Rewritten: '$title' => '$title_guess'" if $DEBUG;
        &get_lyrics( $artist_guess, $title_guess );
    }
}
die "No lyrics found, sorry\n" unless $lyrics;

# Make first letter of every word in string uppercase
sub upcase_first {
    my $str = shift;
    my @tok_up = map(ucfirst, split(/ /, $str));
    my $str_up = join( ' ', @tok_up );
    return $str_up
}
#}}}

##################################################
# Formatting stuff
###################################################{{{
if ($HEADING) {
    my $header  = decode('utf8', "$artist - $title\n");
    $header .= "=" x length($header) . "\n";
    $lyrics = $header . $lyrics;
}

if ($SOURCE) {
    $lyrics .= "\n\n[ retrieved from $source ]\n";
}

if ($INDENT) {
    $lyrics = &indent( $lyrics );
    sub indent {
        my $lyrics = shift;
        my $prepend = ' ' x $INDENT;
        my $temp;
        foreach( split( "\n", $lyrics ) ) {
            s/^/$prepend/;
            $temp .= $_ . "\n";
        }
        return $temp;
    }
}
#}}}

open $file, ">", $dir.$filename;
print $file $lyrics;
close $file;

exit 0;
