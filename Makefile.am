SUBDIRS = src
dist_doc_DATA = README.md
dist_pkgdata_DATA = b-em.cfg

desktopdir = $(datadir)/applications
desktop_DATA = uk.org.stardot.b-em.desktop

appicondir=$(datadir)/icons/hicolor/scalable/apps
appicon_DATA=icon/b-em.svg

appdatadir = $(datadir)/metainfo
appdata_DATA = uk.org.stardot.b-em.metainfo.xml

install-data-hook:
	mkdir -p $(DESTDIR)$(pkgdatadir)
	cp -r ddnoise discs fonts roms tapes $(DESTDIR)$(pkgdatadir)
	find $(DESTDIR)$(pkgdatadir) -type f -print0 | xargs -0 chmod 644
	find $(DESTDIR)$(pkgdatadir) -type d -print0 | xargs -0 chmod 755
