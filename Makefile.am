SUBDIRS = src
dist_doc_DATA = README.md Changes.txt COPYING
dist_pkgdata_DATA = b-em.cfg cmos.bin cmos350.bin cmosa.bin cmosc.bin

install-data-hook:
	mkdir -p $(DESTDIR)$(docdir)/html
	cp $(srcdir)/docs/* $(DESTDIR)$(docdir)/html
	find $(DESTDIR)$(docdir) -type f -print0 | xargs -0 chmod 644
	find $(DESTDIR)$(docdir) -type d -print0 | xargs -0 chmod 755
	mkdir -p $(DESTDIR)$(pkgdatadir)
	cp -r $(srcdir)/ddnoise $(srcdir)/discs $(srcdir)/fonts $(srcdir)/roms $(srcdir)/tapes $(DESTDIR)$(pkgdatadir)
	find $(DESTDIR)$(pkgdatadir) -type f -print0 | xargs -0 chmod 644
	find $(DESTDIR)$(pkgdatadir) -type d -print0 | xargs -0 chmod 755

uninstall-hook:
	rm -rf $(DESTDIR)$(docdir)/html
	rm -rf $(DESTDIR)$(pkgdatadir)/ddnoise $(DESTDIR)$(pkgdatadir)/discs $(DESTDIR)$(pkgdatadir)/fonts
	rm -rf $(DESTDIR)$(pkgdatadir)/roms $(DESTDIR)$(pkgdatadir)/tapes
