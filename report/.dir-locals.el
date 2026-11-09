((org-mode
  . ((eval
      . (progn
          (require 'ox-latex)
          (require 'oc-natbib)
          (require 'seq)
          (let* ((project-root
                  (or (locate-dominating-file default-directory ".dir-locals.el")
                      default-directory))
                 (existing-texinputs (or (getenv "TEXINPUTS") ""))
                 (class-texinputs
                  (mapconcat
                   #'identity
                   (list (expand-file-name project-root) existing-texinputs)
                   path-separator))
                 ;; acmart loads hyperref, graphicx, natbib, amsmath and
                 ;; newtxmath itself; Org's default packages (notably amssymb)
                 ;; clash with it, so suppress them and add only what we need.
                 (custom-class
                  '("sigplan"
                    "\\documentclass[sigplan,screen]{acmart}
[NO-DEFAULT-PACKAGES]
\\usepackage{booktabs}
\\usepackage{longtable}
\\usepackage{capt-of}
\\usepackage[normalem]{ulem}
[PACKAGES]
[EXTRA]"
                    ("\\section{%s}" . "\\section*{%s}")
                    ("\\subsection{%s}" . "\\subsection*{%s}")
                    ("\\subsubsection{%s}" . "\\subsubsection*{%s}")
                    ("\\paragraph{%s}" . "\\paragraph*{%s}"))))
            ;; Keep class lookup and export defaults local to this Org buffer.
            (setq-local process-environment
                        (copy-sequence process-environment))
            (setenv "TEXINPUTS" class-texinputs)
            (setq-local
             org-latex-default-class "sigplan"
             org-export-with-section-numbers t
             org-export-with-toc nil
             ;; acmart requires the abstract *before* \maketitle, so Org must
             ;; not emit it at \begin{document}; the filter below places it.
             org-latex-title-command ""
             org-export-filter-final-output-functions
             (list
              (lambda (output backend info)
                (if (not (org-export-derived-backend-p backend 'latex))
                    output
                  (let* ((kw (org-export-data (plist-get info :keywords) info))
                         (title (concat
                                 (if (string-empty-p kw) ""
                                   (format "\\keywords{%s}\n" kw))
                                 "\\maketitle\n"))
                         (anchor (if (string-match-p "\\\\end{abstract}" output)
                                     "\\end{abstract}\n"
                                   "\\begin{document}\n"))
                         (pos (string-search anchor output)))
                    (if (null pos)
                        output
                      (let ((end (+ pos (length anchor))))
                        (concat (substring output 0 end)
                                title
                                (substring output end))))))))
             org-latex-compiler "pdflatex"
             org-latex-pdf-process
             '("latexmk -f -pdf -interaction=nonstopmode -output-directory=%o %f"))
            (setq-local
             org-latex-classes
             (cons
              custom-class
              (seq-remove
               (lambda (class) (equal (car class) "sigplan"))
               org-latex-classes)))))))))
