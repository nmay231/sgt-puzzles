# -*- makefile -*-

KNIGHT_EXTRA = tree234

knight : [X] GTK COMMON knight nullgame-icon|no-icon
knight : [G] WINDOWS COMMON knight nullgame.res|noicon.res

ALL += knight[COMBINED] KNIGHT_EXTRA

!begin am gtk
GAMES += knight
!end

!begin >list.c
    A(knight) \
!end

!begin >gamedesc.txt
knight:knight.exe:knight:Knights tour puzzle:Find a knights tour that follows the grids hints.
!end
