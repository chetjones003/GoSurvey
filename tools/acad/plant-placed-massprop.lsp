;; plant-placed-massprop.lsp — the AutoCAD side of the Plant 3D placement answer key
;; (D-2026-10-08-b, issue #786).
;;
;; Explodes every Plant 3D pipe, fitting and connector in model space down to plain 3DSOLIDs in their
;; world position (a fitting explodes to an INSERT of its catalog block, which is exploded again; a
;; pipe explodes straight to a solid) and prints AutoCAD's MASSPROP of each. The output becomes
;; samples/example-piping-system.acad-placed.csv, which LibreDwgCadTests matches every solid GoSurvey
;; places against.
;;
;; Run headless as for plant-block-massprop.lsp. Each solid is reported between
;; "WMASS <part handle> 3DSOLID" and "WEND". MASSPROP reports in the current UCS and prints only nine
;; significant digits once a coordinate passes 1e7, so the script switches to a World-aligned UCS
;; whose origin sits near this drawing's geometry: add (42370000, 167596000, 0) back to the reported
;; X/Y extents to get World coordinates.
(setvar "LUPREC" 8)
(setvar "CMDECHO" 1)
(command "_.UCS" "_W")
(command "_.UCS" "_O" "42370000,167596000,0")
(defun g (c ed) (cdr (assoc c ed)))
;; Plant catalog blocks are not explodable by default; allow it on this scratch copy.
(setq b (tblnext "BLOCK" T))
(while b
  (setq br (entget (g 330 (entget (tblobjname "BLOCK" (g 2 b))))))
  (if (assoc 280 br) (entmod (subst (cons 280 1) (assoc 280 br) br)))
  (setq b (tblnext "BLOCK")))
(defun massp (tag e)
  (princ (strcat "\nWMASS " tag " " (g 0 (entget e)) "\n"))
  (command "_.MASSPROP" e "" "_N")
  (princ "\nWEND\n"))
(setq hs nil e (entnext))
(while e
  (setq ed (entget e))
  (if (wcmatch (g 0 ed) "ACPPPIPE,ACPPPIPEINLINEASSET,ACPPCONNECTOR") (setq hs (cons (g 5 ed) hs)))
  (setq e (entnext e)))
(foreach h (reverse hs)
  (setq before (entlast))
  (command "_.EXPLODE" (handent h) "")
  (setq news nil e (entnext before))
  (while e (setq news (cons e news) e (entnext e)))
  (foreach n news
    (setq ty (g 0 (entget n)))
    (cond
      ((= ty "3DSOLID") (massp h n))
      ((= ty "INSERT")
        (setq before2 (entlast))
        (command "_.EXPLODE" n "")
        (setq e2 (entnext before2))
        (while e2 (if (= (g 0 (entget e2)) "3DSOLID") (massp h e2)) (setq e2 (entnext e2)))))))
(princ "\nWORLD done")
(princ)
