(if (< 1 (count-windows))
     (delete-other-windows (selected-window)))
 (catch 'tag
   (while t
     (prefer-coding-system 'utf-8)
     (setq coding-system-for-read 'utf-8)
     (setq coding-system-for-write 'utf-8)
     (c++-mode)
     (setq tab-width 4)
     (setq c-basic-offset 4)
     (untabify (point-min) (point-max))
     (if buffer-file-name  ; nil for *scratch* buffer
         (progn
           (write-file buffer-file-name)
           (kill-buffer (current-buffer)))
       (throw 'tag t))))
