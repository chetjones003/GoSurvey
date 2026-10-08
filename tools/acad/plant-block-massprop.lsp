;; plant-block-massprop.lsp — the AutoCAD side of the REQ-320 increment 2 answer key (issue #786).
;;
;; For every block definition holding a 3DSOLID, prints the solid's handle, its block, and AutoCAD's
;; own MASSPROP of it in block-definition coordinates. The output becomes
;; samples/example-piping-system.acad-blocks.csv, which LibreDwgCadTests compares every ACIS body
;; GoSurvey imports against.
;;
;; Run headless (from PowerShell, with the working directory OUTSIDE the repository — accoreconsole
;; drops error reports there):
;;   accoreconsole.exe /i <copy of example-piping-system.dwg> /s run.scr /l en-US
;; where run.scr holds one line:  (load "C:/path/to/plant-block-massprop.lsp")
;; Each solid is reported between "MASSH <handle> 3DSOLID blk=<name>" and "MASSEND". The blocks are
;; Plant 3D catalog blocks, which are not explodable by default; the script makes each one
;; explodable on the scratch copy, inserts it exploded at the origin, measures it and erases it.
(setvar "CMDECHO" 1)
(setvar "INSUNITS" 0)
(setvar "LUPREC" 8)
(defun solid-handle (blk / e d h)
  (setq e (cdr (assoc -2 (tblsearch "BLOCK" blk))))
  (while e
    (setq d (entget e))
    (if (= (cdr (assoc 0 d)) "3DSOLID") (setq h (cdr (assoc 5 d))))
    (setq e (entnext e)))
  h)
(defun mass-one (blk / h sol br)
  (setq h (solid-handle blk))
  (if h
    (progn
      (setq br (entget (cdr (assoc 330 (entget (tblobjname "BLOCK" blk))))))
      (if (assoc 280 br) (entmod (subst (cons 280 1) (assoc 280 br) br)))
      (command "_.-INSERT" (strcat "*" blk) "0,0,0" "1" "0")
      (setq sol (entlast))
      (princ (strcat "\nMASSH " h " " (cdr (assoc 0 (entget sol))) " blk=" blk "\n"))
      (command "_.MASSPROP" sol "" "_N")
      (princ "\nMASSEND\n")
      (command "_.ERASE" sol ""))))
(setq blks nil b (tblnext "BLOCK" T))
(while b (setq blks (cons (cdr (assoc 2 b)) blks)) (setq b (tblnext "BLOCK")))
(foreach n (reverse blks) (mass-one n))
(princ "\nMASS done")
(princ)
