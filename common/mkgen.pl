#!/usr/bin/perl -w

open MK, "> Makefile.am";

require 'plugin-defs.pl';

$bins = ""; $opts = "";

foreach (sort keys %plugins) {
    $bins .= "\t";
    if (exists $plugins{$_}->{optional}) {
	$bins .= "\$(\U$_\E)";
	$opts .= "\t$_ \\\n";
    }
    else {
	$bins .= $_;
    }
    $bins .= " \\\n";
}

$extra = "";
foreach (@extra) { $extra .= "\t$_\t\\\n" }
if ($extra) {
    $extra =~ s/\t\\\n$//s;
    $extra = "\t\\\n$extra";
}

foreach ($bins, $opts) { s/ \\\n$//s }

print MK <<EOT;
## This file is autogenerated by mkgen.pl and plugin-defs.pl

libexecdir = \$(gimpplugindir)/plug-ins

EXTRA_DIST = \\
	mkgen.pl	\\
	plugin-defs.pl$extra

AM_CPPFLAGS = \\
	-DLOCALEDIR=\\""\$(localedir)"\\"

INCLUDES = \\
	-I\$(top_srcdir)		\\
	\$(GTK_CFLAGS)		\\
	-I\$(includedir)

libexec_PROGRAMS = \\
$bins

EXTRA_PROGRAMS = \\
$opts

install-\%: \%
	\@\$(NORMAL_INSTALL)
	\$(mkinstalldirs) \$(DESTDIRS)\$(libexecdir)
	\@if test -f \$<; then \\
	  echo " \$(LIBTOOL)  --mode=install \$(INSTALL_PROGRAM) \$< \$(DESTDIR)\$(libexecdir)/`echo \$<|sed 's/\$(EXEEXT)\$\$//'|sed '\$(transform)'|sed 's/\$\$/\$(EXEEXT)/'`"; \\
	  \$(LIBTOOL)  --mode=install \$(INSTALL_PROGRAM) \$< \$(DESTDIR)\$(libexecdir)/`echo \$<|sed 's/\$(EXEEXT)\$\$//'|sed '\$(transform)'|sed 's/\$\$/\$(EXEEXT)/'`; \\
	else :; fi
EOT

foreach (sort keys %plugins) {
    my $libgimp = "";

    $libgimp .= "\$(top_builddir)/libgimp/libgimp.la";
    if (exists $plugins{$_}->{ui}) {
	$libgimp .= "\t\\\n\t$libgimp";
	$libgimp =~ s/gimp\./gimpui./;
    }

    my $optlib = ""; 
    if (exists $plugins{$_}->{optional}) {
	my $name = exists $plugins{$_}->{libopt} ? $plugins{$_}->{libopt} : $_;
	$optlib = "\n\t\$(LIB\U$name\E)\t\t\t\t\\";
    }

    if (exists $plugins{$_}->{libsupp}) {
	my @lib = split(/:/, $plugins{$_}->{libsupp});
	foreach $lib (@lib) {
	    $libgimp = "\$(top_builddir)/plug-ins/$lib/lib$lib.a\t\\\n\t$libgimp";
	}
	$libgimp =~ s@gck/libgck\.a@libgck/gck/libgck.la@;
    }

    print MK <<EOT;

${_}_SOURCES = \\
	$_.c

${_}_LDADD = \\
	$libgimp	\\$optlib
	\$(\U$plugins{$_}->{libdep}\E_LIBS)				\\
	\$(INTLLIBS)
EOT
}

close MK;
