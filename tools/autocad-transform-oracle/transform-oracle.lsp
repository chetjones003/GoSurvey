;; AutoCAD transform oracle (ADR-0011 / issue #1577).
;;
;; Builds one entity of each kind, runs MOVE / COPY / ROTATE / ROTATE3D / SCALE / MIRROR / STRETCH on
;; it, and prints the resulting DXF groups. The GoSurvey entity-kind tests use these numbers as their
;; expected values. Run it with run-transform-oracle.ps1; the results land in
;; build/artifacts/autocad-transform-oracle/oracle.txt.
;;
;; MIRRTEXT 0 and MIRRHATCH 0 are AutoCAD's defaults and the settings GoSurvey follows.

(defun oracle-dump (tag e / d)
  (setq d (entget e))
  (princ (strcat "\n### " tag " "))
  (foreach g d
    (if (member (car g) '(0 2 10 11 12 13 14 40 41 42 43 50 52 53 71 72 210))
      (progn (princ g) (princ " "))))
  (princ))

(defun oracle-hatch ( / pl)
  (command "_.PLINE" "16,12" "20,12" "16,15" "_C")
  (setq pl (entlast))
  (command "_.-HATCH" "_P" "ANSI31" "2" "15" "_S" pl "" "")
  (entdel pl)
  (entlast))

(defun oracle-mleader () (command "_.MLEADER" "16,12" "19,14" "ML") (entlast))
(defun oracle-text (rot) (command "_.TEXT" "16,12" "1" rot "AB") (entlast))
(defun oracle-mtext () (command "_.-MTEXT" "16,15" "_J" "_TL" "20,12" "NOTE" "") (entlast))
(defun oracle-diml () (command "_.DIMLINEAR" "16,12" "20,12" "18,14") (entlast))
(defun oracle-table () (command "_.-TABLE" "2" "2" "16,12") (entlast))
(defun oracle-insert (rot) (command "_.-INSERT" "ORACLEBLK" "16,12" "1" "1" rot) (entlast))
(defun oracle-ellipse () (command "_.ELLIPSE" "_C" "16,12" "19,12" "1.5") (entlast))

(defun oracle-stretch ( / ss)
  (setq ss (ssget "_C" '(25 25) '(14 5)))
  (command "_.STRETCH" ss "" "0,0" "5,3"))

;; Run op on a freshly made entity, then print the original and any new entity.
(defun oracle-case (tag mk op / e n)
  (setq e (eval mk))
  (setq n (entlast))
  (eval op)
  (oracle-dump (strcat tag "-orig") e)
  (if (not (equal n (entlast)))
    (oracle-dump (strcat tag "-new") (entlast))
    (princ (strcat "\n### " tag " no-new-entity")))
  (command "_.ERASE" "_ALL" ""))

(defun c:TRANSFORMORACLE ()
  (setvar "CMDECHO" 0)
  (setvar "OSMODE" 0)
  (setvar "MIRRTEXT" 0)
  (setvar "MIRRHATCH" 0)
  (command "_.ZOOM" "_W" "-60,-60" "60,60")
  (command "_.LINE" "0,0" "1,1" "")
  (command "_.-BLOCK" "ORACLEBLK" "0,0" (entlast) "")

  (oracle-case "line-scale-z" '(progn (command "_.LINE" "10,12,3" "20,12,4" "") (entlast))
               '(command "_.SCALE" e "" "0,0,0" "2"))
  (oracle-case "circle-scale-z" '(progn (command "_.CIRCLE" "16,12,3" "2") (entlast))
               '(command "_.SCALE" e "" "0,0,0" "2"))
  (oracle-case "text-scale-z" '(progn (command "_.TEXT" "16,12,3" "1" "0" "AB") (entlast))
               '(command "_.SCALE" e "" "0,0,0" "2"))

  (oracle-case "hatch-rotate" '(oracle-hatch) '(command "_.ROTATE" e "" "0,0" "-90"))
  (oracle-case "hatch-rotcopy" '(oracle-hatch) '(command "_.ROTATE" e "" "0,0" "_C" "-90"))
  (oracle-case "hatch-scale" '(oracle-hatch) '(command "_.SCALE" e "" "0,0" "2"))
  (oracle-case "hatch-mirror" '(oracle-hatch) '(command "_.MIRROR" e "" "0,0" "0,10" "_N"))
  (oracle-case "hatch-rot3d" '(oracle-hatch) '(command "_.ROTATE3D" e "" "_Y" "0,0,0" "90"))

  (oracle-case "mleader-rotate" '(oracle-mleader) '(command "_.ROTATE" e "" "0,0" "-90"))
  (oracle-case "mleader-scale" '(oracle-mleader) '(command "_.SCALE" e "" "0,0" "2"))
  (oracle-case "mleader-mirror" '(oracle-mleader) '(command "_.MIRROR" e "" "0,0" "0,10" "_N"))
  (oracle-case "mleader-rot3d" '(oracle-mleader) '(command "_.ROTATE3D" e "" "_Y" "0,0,0" "90"))

  (oracle-case "ellipse-rot3d" '(oracle-ellipse) '(command "_.ROTATE3D" e "" "_Y" "0,0,0" "90"))

  (oracle-case "text-rotate" '(oracle-text "0") '(command "_.ROTATE" e "" "0,0" "-90"))
  (oracle-case "text-mirror" '(oracle-text "0") '(command "_.MIRROR" e "" "0,0" "0,10" "_N"))
  (oracle-case "text-mirror-r30" '(oracle-text "30") '(command "_.MIRROR" e "" "0,0" "0,10" "_N"))
  (oracle-case "text-rot3d" '(oracle-text "0") '(command "_.ROTATE3D" e "" "_Y" "0,0,0" "90"))
  (oracle-case "mtext-mirror" '(oracle-mtext) '(command "_.MIRROR" e "" "0,0" "0,10" "_N"))
  (oracle-case "mtext-rotate" '(oracle-mtext) '(command "_.ROTATE" e "" "0,0" "-90"))

  (oracle-case "dimlinear-rotate90" '(oracle-diml) '(command "_.ROTATE" e "" "0,0" "-90"))
  (oracle-case "dimlinear-rotate30" '(oracle-diml) '(command "_.ROTATE" e "" "0,0" "30"))
  (oracle-case "dimlinear-mirror" '(oracle-diml) '(command "_.MIRROR" e "" "0,0" "0,10" "_N"))
  (oracle-case "dimlinear-scale" '(oracle-diml) '(command "_.SCALE" e "" "0,0" "2"))
  (oracle-case "dimlinear-rot3d" '(oracle-diml) '(command "_.ROTATE3D" e "" "_Y" "0,0,0" "90"))

  (oracle-case "table-rotate" '(oracle-table) '(command "_.ROTATE" e "" "0,0" "-90"))
  (oracle-case "table-mirror" '(oracle-table) '(command "_.MIRROR" e "" "0,0" "0,10" "_N"))
  (oracle-case "table-scale" '(oracle-table) '(command "_.SCALE" e "" "0,0" "2"))
  (oracle-case "table-rot3d" '(oracle-table) '(command "_.ROTATE3D" e "" "_Y" "0,0,0" "90"))

  (oracle-case "insert-mirror-r30" '(oracle-insert "30") '(command "_.MIRROR" e "" "0,0" "0,10" "_N"))
  (oracle-case "insert-rot3d" '(oracle-insert "30") '(command "_.ROTATE3D" e "" "_Y" "0,0,0" "90"))

  (oracle-case "point-stretch" '(progn (command "_.POINT" "16,12") (entlast)) '(oracle-stretch))
  (oracle-case "spline-stretch" '(progn (command "_.SPLINE" "10,12" "15,14" "20,12" "" "" "") (entlast))
               '(oracle-stretch))
  (oracle-case "insert-stretch-in" '(oracle-insert "0") '(oracle-stretch))
  (oracle-case "insert-stretch-out" '(progn (command "_.-INSERT" "ORACLEBLK" "13.5,12" "1" "1" "0") (entlast))
               '(oracle-stretch))
  (princ "\n### done")
  (princ))
