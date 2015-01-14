RST2HTML := rst2html
RST2PDF := rst2pdf
RST_OPTS := -l en_US --smart-quotes=1

SRC_RST=$(wildcard *.rst)

help:
	@echo "make [html|pdf]"

html: $(SRC_RST:.rst=.html)
.PHONY: html

%.html: %.rst
	@echo Building $@ from $<...
	@$(RST2HTML) $(RST_OPTS) $< $@

pdf: $(SRC_RST:.rst=.pdf)
.PHONY: pdf

%.pdf: %.rst
	@echo Building $@ from $<...
	@rst2pdf $(RST_OPTS) -o $@ $<

clean:
	rm -f $(SRC_RST:.rst=.html)
	rm -f $(SRC_RST:.rst=.pdf)
.PHONY: clean
