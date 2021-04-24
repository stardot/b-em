SUBDIRS = src
dist_doc_DATA = README.md
dist_pkgdata_DATA = b-em.cfg

desktopdir = $(datadir)/applications
desktop_DATA = uk.org.stardot.b-em.desktop

install-data-hook:
	mkdir -p $(DESTDIR)$(pkgdatadir)
	cp -r ddnoise discs fonts roms tapes $(DESTDIR)$(pkgdatadir)
	find $(DESTDIR)$(pkgdatadir) -type f -print0 | xargs -0 chmod 644
	find $(DESTDIR)$(pkgdatadir) -type d -print0 | xargs -0 chmod 755
