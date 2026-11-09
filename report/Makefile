ORG := report.org
TEX := $(ORG:.org=.tex)
PDF := $(ORG:.org=.pdf)

.PHONY: all clean

all: $(PDF)

# Export via Emacs so .dir-locals.el (class, title handling) is applied.
$(TEX): $(ORG) .dir-locals.el
	emacs --batch --eval "(setq enable-local-variables :all enable-local-eval t)" \
	  --visit=$(ORG) --funcall=org-latex-export-to-latex

$(PDF): $(TEX) references.bib
	latexmk -f -pdf -interaction=nonstopmode $(TEX)

clean:
	latexmk -C $(TEX)
	rm -f $(TEX)
