SUBDIRS = src
dist_doc_DATA = README.md
dist_pkgdata_DATA = b-em.cfg

install-data-hook:
	mkdir -p $(DESTDIR)$(pkgdatadir)
	cp -r ddnoise discs roms tapes $(DESTDIR)$(pkgdatadir)
	find $(DESTDIR)$(pkgdatadir) -type f -print0 | xargs -0 chmod 444
	find $(DESTDIR)$(pkgdatadir) -type d -print0 | xargs -0 chmod 555
