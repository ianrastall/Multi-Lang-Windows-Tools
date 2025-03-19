#!perl
use strict;
use warnings;
use File::Spec;
use Time::HiRes qw(time);
use Win32::API;

#---------------------------
# 1) Define API calls we need
#---------------------------
my $GetDriveType = Win32::API->new('kernel32', 'GetDriveTypeA', 'P', 'N')
    or die "Failed to load GetDriveTypeA from kernel32.dll";
my $GetFileAttributes = Win32::API->new('kernel32', 'GetFileAttributesA', 'P', 'N')
    or die "Failed to load GetFileAttributesA from kernel32.dll";

# Constants for drive types
use constant DRIVE_REMOVABLE => 2;
use constant DRIVE_FIXED     => 3;

# File attribute constants
use constant FILE_ATTRIBUTE_HIDDEN    => 0x2;
use constant FILE_ATTRIBUTE_SYSTEM    => 0x4;
use constant FILE_ATTRIBUTE_TEMPORARY => 0x100;
use constant FILE_ATTRIBUTE_REPARSE   => 0x400;

my $outfile = 'largest_files.txt';

#---------------------------------------
# Enumerate drives and filter by type
#---------------------------------------
sub enumerate_drives {
    print "[Drive Scan]\n";
    my @drives;
    for my $letter ('A' .. 'Z') {
        my $drive = "$letter:\\";
        next if (! -e $drive);

        my $drive_type = $GetDriveType->Call($drive);
        if ($drive_type == DRIVE_REMOVABLE || $drive_type == DRIVE_FIXED) {
            print "Including drive: $drive\n";
            push @drives, $drive;
        }
    }
    return @drives;
}

#---------------------------------------
# Check if file should be skipped (attrs)
#---------------------------------------
sub should_skip {
    my ($path) = @_;
    my $attrs = $GetFileAttributes->Call($path);
    # 0xFFFFFFFF (4294967295) means "invalid" – we skip checking
    return 0 if !defined $attrs or $attrs == 0xFFFFFFFF;

    if ($attrs & (FILE_ATTRIBUTE_HIDDEN |
                  FILE_ATTRIBUTE_SYSTEM |
                  FILE_ATTRIBUTE_TEMPORARY |
                  FILE_ATTRIBUTE_REPARSE)) {
        return 1;
    }
    return 0;
}

#---------------------------------------
# Recursively gather files
#---------------------------------------
sub scan_directory {
    my ($start, $files) = @_;
    opendir my $dh, $start or return;
    my @entries = readdir $dh;
    closedir $dh;

    foreach my $entry (@entries) {
        next if $entry eq '.' or $entry eq '..';
        my $full = File::Spec->catfile($start, $entry);

        next if should_skip($full);

        if (-d $full) {
            scan_directory($full, $files);
        } else {
            my $size = -s $full;
            push @$files, { path => $full, size => $size }
                if defined $size;
        }
    }
}

#---------------------------------------
# Main script
#---------------------------------------
print "File Scanner\n";
print "----------------------------------------\n";

# Overwrite output file
open my $outfh, '>', $outfile or die "Cannot open $outfile: $!";
close $outfh;

my @drives = enumerate_drives();
if (!@drives) {
    warn "No suitable drives found!\n";
    print "Press Enter to exit...";
    <STDIN>;
    exit 1;
}

foreach my $drive (@drives) {
    print "\nProcessing $drive\n";
    my $start_time = time();

    my @collected;
    scan_directory($drive, \@collected);

    my $elapsed = time() - $start_time;
    printf "Scanned %s in %.1f seconds\n", $drive, $elapsed;
    printf "Found %d files\n", scalar @collected;

    if (@collected) {
        @collected = sort { $b->{size} <=> $a->{size} } @collected;

        open my $fh, '>>', $outfile or die "Cannot open $outfile for append: $!";
        my $drive_display = substr($drive, 0, 2);  # e.g. 'C:'
        print $fh "Largest files on $drive_display:\n";

        my $limit = @collected < 100 ? @collected : 100;
        for (my $i = 0; $i < $limit; $i++) {
            my $mb = $collected[$i]{size} / (1024*1024);
            printf $fh "%s: %.2f MB\n", $collected[$i]{path}, $mb;
        }
        print $fh "\n";
        close $fh;
    }
}

print "\nScan complete. Results saved to $outfile\n";
print "Press Enter to exit...";
<STDIN>;
