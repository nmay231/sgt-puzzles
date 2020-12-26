# -*- makefile -*-

KNIGHT_EXTRA = tdq

knight : [X] GTK COMMON knight KNIGHT_EXTRA nullgame-icon|no-icon
knight : [G] WINDOWS COMMON knight KNIGHT_EXTRA nullgame.res|noicon.res

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
