/* macro.inc: macros de spimdisasm para ensamblar los datos del decomp con GNU as (x86/ARM) */
.macro glabel label
    .global \label
    \label:
.endm
.macro dlabel label
    .global \label
    \label:
.endm
.macro jlabel label
    .global \label
    \label:
.endm
.macro alabel label
    .global \label
    \label:
.endm
.macro enddlabel label
.endm
.macro endlabel label
.endm
.macro nonmatching label, size=1
.endm
.macro late_rodata_alignment a
.endm
