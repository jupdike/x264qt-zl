#!/bin/sh -v
# Based on createStaticLibs.sh from Perian
PATH=$PATH:/usr/local/bin:/usr/bin:/sw/bin:/opt/local/bin
buildid_x264="r`svn info x264 | grep -F Revision | awk '{print $2}'`"

generalConfigureOptions="--enable-pthread --disable-gtk"

if [ "$BUILD_STYLE" = "Development" ] ; then
	extraConfigureOptions="--enable-debug"
fi

OUTPUT_FILE="$BUILT_PRODUCTS_DIR/Universal/buildid"

if [[ -e "$OUTPUT_FILE" ]] ; then
	oldbuildid_x264=`cat "$OUTPUT_FILE"`
else
	oldbuildid_x264="buildme"
fi

if [[ $buildid == "r" ]] ; then
	echo "error: you're using svk. Please ask someone to add svk support to the build system. There's a script in Adium svn that can do this."
	exit 1;
fi

if [ "$buildid_x264" = "$oldbuildid_x264" ] ; then
	echo "Static x264 libs are up-to-date ; not rebuilding"
else
	echo "Static x264 libs are out-of-date ; rebuilding"
	mkdir "$BUILT_PRODUCTS_DIR"
	#######################
	# Intel shlibs
	#######################
	BUILDDIR="$BUILT_PRODUCTS_DIR/intel"
	mkdir "$BUILDDIR"
	MACHINE="i386-apple-darwin"
	export MACHINE
	
	cd "$BUILDDIR"
	cp -Rpf "$SRCROOT/x264/"* "$BUILDDIR"
	patch -p0 < "$SRCROOT/x264config.patch"
	if [ `arch` != i386 ] ; then
		"$BUILDDIR/configure" --extra-ldflags='-arch i386 -isysroot /Developer/SDKs/MacOSX10.4u.sdk' --extra-cflags='-arch i386 -isysroot /Developer/SDKs/MacOSX10.4u.sdk' $extraConfigureOptions $generalConfigureOptions
	else
		"$BUILDDIR/configure" $extraConfigureOptions $generalConfigureOptions
	fi
    make	
	
	#######################
	# PPC shlibs
	#######################
	BUILDDIR="$BUILT_PRODUCTS_DIR/ppc"
	mkdir "$BUILDDIR"
	MACHINE="powerpc-apple-darwin"
	export MACHINE
	
	cd "$BUILDDIR"
	cp -Rpf "$SRCROOT/x264/"* "$BUILDDIR"
	patch -p0 < "$SRCROOT/x264config.patch"
	if [ `arch` = ppc ] ; then
		"$BUILDDIR/configure" $extraConfigureOptions $generalConfigureOptions
	else
		"$BUILDDIR/configure" --extra-ldflags='-arch ppc -isysroot /Developer/SDKs/MacOSX10.4u.sdk' --extra-cflags='-arch ppc -isysroot /Developer/SDKs/MacOSX10.4u.sdk' $extraConfigureOptions $generalConfigureOptions
	fi
    make
	
	#######################
	# lipo shlibs
	#######################
	BUILDDIR="$BUILT_PRODUCTS_DIR/Universal"
	INTEL="$BUILT_PRODUCTS_DIR/intel"
	PPC="$BUILT_PRODUCTS_DIR/ppc"
	rm -rf "$BUILDDIR"
	mkdir "$BUILDDIR"
	
	echo lipo -create "$INTEL/libx264.a" "$PPC/libx264.a" -output "$BUILDDIR/libx264.a"
    lipo -create "$INTEL/libx264.a" "$PPC/libx264.a" -output "$BUILDDIR/libx264.a"
	
	if [ -e "$BUILDDIR/libx264.a" ] ; then
    	echo -n "$buildid_x264" > $OUTPUT_FILE
    fi
fi

mkdir "$SYMROOT/Universal" || true
cp "$BUILT_PRODUCTS_DIR/Universal"/* "$SYMROOT/Universal"
if [ "$BUILD_STYLE" = "Deployment" ] ; then
	strip -S "$SYMROOT/Universal"/*.a
fi
ranlib "$SYMROOT/Universal"/*.a
