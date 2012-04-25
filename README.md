x264qt-zl
=========

This project is a fork of <a
href="http://developer.berlios.de/projects/x264qtcodec/">x264
QuickTime Codec</a>, updated to work with
a more recent <a href="http://git.videolan.org/?p=x264.git;a=summary">x264 code base</a>
(x264-snapshot-20120119-2245) [1] and reissued under
<a href="http://www.gnu.org/licenses/gpl-2.0.html">the (same) GPL</a> for your hacking pleasure.

[1] available here: ftp://ftp.videolan.org/pub/videolan/x264/snapshots/x264-snapshot-20120119-2245.tar.bz2

Be advised that this little project takes two liberties (easily
changed by forking and recompiling):

  1. It is hardcoded to use the Zero Latency (real-time) setting
  because that is what I needed.

  2. It changes the four-cc code from 'avc1' to 'x264' in order to
  avoid a collision with the built-in OS X H.264 encoder.

Another caveat: this build is Intel-only, and I have only tested it on Snow Leopard.

Binary download
===============

A binary download is available <a
href="x264qt-zl/raw/master/zips/2012-02-13-x264Codec.component.zip">here</a>.

To use, simply un-zip and move the x264Codec.component to
/Library/QuickTime (This may require administrator permissions.)  (Or
you should be able to use your user's Library/QuickTime folder.)
