; ATC - "Arcade Test Cabinet": a tiny NES program used to validate the
; multiplayer arcade end to end with lawful content we wrote ourselves.
;
; One source, four builds (MODE_VAL): 0 versus, 1 co-op (4 players via Four
; Score), 2 turns (one shared controller), 3 solo. The game rules are trivial
; on purpose; what matters is that every event the arcade cares about
; (credits, joins, continues, individual game over, rounds, match results,
; draws, stage boundaries, turn ownership, scores) lives at a FIXED, documented
; RAM address so a verified adapter can read it. See atc-ram-map.md.
;
; Controls (NES pad): D-pad moves your block, A scores (+10, or a "hit" in
; versus), B loses a life (co-op/solo) or ends the turn (turns mode),
; SELECT inserts a coin (turns mode: changes participant count on the title),
; START joins / continues / starts.
;
; License: MIT (see native/testroms/LICENSE).

.ifndef MODE_VAL
MODE_VAL = 1
.endif

; ---------------------------------------------------------------- RAM map
SIG       = $0300   ; 4 bytes "ATC1"
MODE      = $0304   ; 0 versus, 1 coop, 2 turns, 3 solo
GSTATE    = $0305   ; 0 title/waiting, 1 playing, 2 round over, 3 match/game over, 4 stage clear
FRAMELO   = $0306
FRAMEHI   = $0307
CREDITS   = $0308   ; shared credit pool (0-9)
ROUND     = $0309   ; versus round / co-op stage / turns round
WINNER    = $030A   ; last match: 0 none, 1-4 player, $FF draw
MATCHID   = $030B   ; increments when a match / turns game ends
TURNP     = $030C   ; turns mode: whose turn (0-3)
NPART     = $030D   ; turns mode: participants (2-4)
TIMERLO   = $030E   ; state timer (frames)
TIMERHI   = $030F
PBASE     = $0310   ; 4 players x 8 bytes:
P_STATE   = 0       ;   0 not playing, 1 playing, 2 game over (continue countdown)
P_LIVES   = 1
P_SC0     = 2       ;   score, 24-bit binary little endian
P_SC1     = 3
P_SC2     = 4
P_WINS    = 5       ;   versus: rounds won in current match
P_X       = 6
P_Y       = 7
GOCOUNT   = $0330   ; 4: game overs per player
CONTCNT   = $0334   ; 4: continues used per player
CDOWN     = $0338   ; 4: continue countdown seconds
CDSUB     = $033C   ; 4: continue countdown frames
HITS      = $0340   ; 4: versus hits this round
TURNS     = $0344   ; 4: turns mode, turns completed per player
DRAWS     = $0348   ; versus: drawn rounds in this match
TEXTBUF   = $0400   ; 4 rows x 32 tiles, copied to the screen in NMI
OAMBUF    = $0200

; zero page
tmp0      = $00
tmp1      = $01
tmp2      = $02
tmp3      = $03
ptr       = $04     ; 2 bytes
num0      = $06     ; 3 bytes: number being printed
cur       = $09     ; current player index
pad       = $10     ; 4
prev      = $14     ; 4
edge      = $18     ; 4 newly pressed
nmi_ready = $20
nmi_done  = $21
digits    = $22     ; 6

; pad bits after reading (bit7..0) = A B Select Start Up Down Left Right
BTN_A     = $80
BTN_B     = $40
BTN_SEL   = $20
BTN_START = $10
BTN_UP    = $08
BTN_DOWN  = $04
BTN_LEFT  = $02
BTN_RIGHT = $01

.if MODE_VAL = 0
NPLAYERS = 2
.elseif MODE_VAL = 1
NPLAYERS = 4
.elseif MODE_VAL = 2
NPLAYERS = 4
.else
NPLAYERS = 1
.endif

.segment "HEADER"
  .byte "NES", $1A, 1, 1, $00, $00, 0, 0, 0, 0, 0, 0, 0, 0

.segment "CODE"

reset:
  sei
  cld
  ldx #$40
  stx $4017
  ldx #$FF
  txs
  inx
  stx $2000
  stx $2001
  stx $4010
@vb1:
  bit $2002
  bpl @vb1
  ; clear RAM
  lda #0
  tax
@clr:
  sta $0000,x
  sta $0100,x
  sta $0300,x
  sta $0400,x
  sta $0500,x
  sta $0600,x
  sta $0700,x
  inx
  bne @clr
  lda #$FF
@clroam:
  sta OAMBUF,x
  inx
  bne @clroam
@vb2:
  bit $2002
  bpl @vb2

  ; signature and mode
  lda #'A'
  sta SIG
  lda #'T'
  sta SIG+1
  lda #'C'
  sta SIG+2
  lda #'1'
  sta SIG+3
  lda #MODE_VAL
  sta MODE
  lda #2
  sta NPART
  jsr reset_players

  ; palettes
  lda #$3F
  sta $2006
  lda #$00
  sta $2006
  ldx #0
@pal:
  lda palette,x
  sta $2007
  inx
  cpx #32
  bne @pal

  ; nametable: spaces, attributes 0
  lda #$20
  sta $2006
  lda #$00
  sta $2006
  ldy #4
  ldx #0
  lda #' '
@nt:
  sta $2007
  inx
  bne @nt
  dey
  bne @nt
  ; (the last 64 bytes were attributes; ' ' = $20 -> palette bits; rewrite them)
  lda #$23
  sta $2006
  lda #$C0
  sta $2006
  ldx #64
  lda #0
@at:
  sta $2007
  dex
  bne @at

  ; sound: enable pulse 1
  lda #$01
  sta $4015

  lda #0
  sta $2005
  sta $2005
  lda #$80
  sta $2000
  lda #$1E
  sta $2001

main_loop:
  lda #0
  sta nmi_done
@wait:
  lda nmi_done
  beq @wait
  jsr read_pads
  jsr update
  jsr build_text
  jsr build_sprites
  lda #1
  sta nmi_ready
  jmp main_loop

; ---------------------------------------------------------------- NMI
nmi:
  pha
  txa
  pha
  tya
  pha
  lda nmi_ready
  bne :+
  jmp @skip
:
  lda #0
  sta $2003
  lda #>OAMBUF
  sta $4014
  ; blit 4 text rows
  ldy #0
@row:
  lda rowhi,y
  sta $2006
  lda rowlo,y
  sta $2006
  tya
  asl
  asl
  asl
  asl
  asl
  tax
  .repeat 32, I
  lda TEXTBUF+I,x
  sta $2007
  .endrepeat
  iny
  cpy #4
  beq :+
  jmp @row
:
  lda #0
  sta $2005
  sta $2005
  lda #$80
  sta $2000
  lda #0
  sta nmi_ready
@skip:
  inc FRAMELO
  bne @nf
  inc FRAMEHI
@nf:
  lda #1
  sta nmi_done
  pla
  tay
  pla
  tax
  pla
irq:
  rti

rowhi: .byte $20, $20, $20, $21
rowlo: .byte $40, $80, $A0, $A0

; ---------------------------------------------------------------- input
; Reads 4 pads (Four Score layout: $4016 = pads 1 and 3, $4017 = pads 2 and 4).
read_pads:
  ldx #3
@cp:
  lda pad,x
  sta prev,x
  dex
  bpl @cp
  lda #1
  sta $4016
  lda #0
  sta $4016
  ldx #8
@r1:
  lda $4016
  lsr
  rol pad+0
  lda $4017
  lsr
  rol pad+1
  dex
  bne @r1
  ldx #8
@r2:
  lda $4016
  lsr
  rol pad+2
  lda $4017
  lsr
  rol pad+3
  dex
  bne @r2
  ; drain signature bits
  ldx #8
@r3:
  lda $4016
  lda $4017
  dex
  bne @r3
  ldx #3
@ed:
  lda prev,x
  eor #$FF
  and pad,x
  sta edge,x
  dex
  bpl @ed
.if NPLAYERS < 4
  ; players beyond NPLAYERS do not exist in this build
  lda #0
  ldx #NPLAYERS
@zero:
  sta pad,x
  sta edge,x
  inx
  cpx #4
  bne @zero
.endif
  rts

; ---------------------------------------------------------------- helpers
; X = player index -> Y = player block offset
pofs:
  txa
  asl
  asl
  asl
  tay
  rts

reset_players:
  ldx #0
@l:
  jsr pofs
  lda #0
  sta PBASE+P_STATE,y
  sta PBASE+P_LIVES,y
  sta PBASE+P_WINS,y
  sta HITS,x
  sta TURNS,x
  lda start_x,x
  sta PBASE+P_X,y
  lda #120
  sta PBASE+P_Y,y
  inx
  cpx #4
  bne @l
  rts

clear_score: ; X = player
  jsr pofs
  lda #0
  sta PBASE+P_SC0,y
  sta PBASE+P_SC1,y
  sta PBASE+P_SC2,y
  rts

; add A to player X's 24-bit score
add_score:
  pha
  jsr pofs
  pla
  clc
  adc PBASE+P_SC0,y
  sta PBASE+P_SC0,y
  bcc @d
  lda PBASE+P_SC1,y
  adc #0
  sta PBASE+P_SC1,y
  bcc @d
  lda PBASE+P_SC2,y
  adc #0
  sta PBASE+P_SC2,y
@d:
  rts

; beep with a pitch per player (X)
beep:
  lda #%10011111
  sta $4000
  lda #0
  sta $4001
  lda beep_lo,x
  sta $4002
  lda #%00010000
  sta $4003
  rts

; coin: SELECT inserts a credit (max 9)
coins:
  ldx #0
@l:
  lda edge,x
  and #BTN_SEL
  beq @n
  lda CREDITS
  cmp #9
  bcs @n
  inc CREDITS
@n:
  inx
  cpx #NPLAYERS
  bne @l
  rts

; join player X with a credit (fresh: score 0). Carry set if joined.
join:
  lda CREDITS
  beq @no
  dec CREDITS
  jsr clear_score
  jsr pofs
  lda #1
  sta PBASE+P_STATE,y
  lda #3
  sta PBASE+P_LIVES,y
  lda #0
  sta PBASE+P_WINS,y
  sta HITS,x
  sec
  rts
@no:
  clc
  rts

; continue player X with a credit (score kept). Carry set if continued.
continue:
  lda CREDITS
  beq @no
  dec CREDITS
  jsr pofs
  lda #1
  sta PBASE+P_STATE,y
  lda #3
  sta PBASE+P_LIVES,y
  inc CONTCNT,x
  sec
  rts
@no:
  clc
  rts

; move player X's block with the d-pad
move:
  jsr pofs
  lda pad,x
  and #BTN_LEFT
  beq @r
  lda PBASE+P_X,y
  cmp #9
  bcc @r
  sec
  sbc #2
  sta PBASE+P_X,y
@r:
  lda pad,x
  and #BTN_RIGHT
  beq @u
  lda PBASE+P_X,y
  cmp #240
  bcs @u
  clc
  adc #2
  sta PBASE+P_X,y
@u:
  lda pad,x
  and #BTN_UP
  beq @dn
  lda PBASE+P_Y,y
  cmp #64
  bcc @dn
  sec
  sbc #2
  sta PBASE+P_Y,y
@dn:
  lda pad,x
  and #BTN_DOWN
  beq @e
  lda PBASE+P_Y,y
  cmp #200
  bcs @e
  clc
  adc #2
  sta PBASE+P_Y,y
@e:
  rts

; continue countdown for player X in state 2; START continues.
countdown:
  lda edge,x
  and #BTN_START
  beq @tick
  jsr continue
  bcs @done
@tick:
  dec CDSUB,x
  bne @done
  lda #60
  sta CDSUB,x
  dec CDOWN,x
  bne @done
  jsr pofs
  lda #0
  sta PBASE+P_STATE,y
@done:
  rts

; player X enters game over (state 2, 10 second countdown)
game_over_player:
  jsr pofs
  lda #2
  sta PBASE+P_STATE,y
  lda #10
  sta CDOWN,x
  lda #60
  sta CDSUB,x
  inc GOCOUNT,x
  rts

inc_timer:
  inc TIMERLO
  bne @d
  inc TIMERHI
@d:
  rts

clr_timer:
  lda #0
  sta TIMERLO
  sta TIMERHI
  rts

; ---------------------------------------------------------------- update
update:
.if MODE_VAL = 0
  jmp update_versus
.elseif MODE_VAL = 2
  jmp update_turns
.else
  jmp update_coop
.endif

; ---- co-op / solo
update_coop:
  jsr coins
  lda GSTATE
  cmp #1
  beq @play
  cmp #3
  beq @over
  cmp #4
  beq @clear
  ; title: START joins and starts
  ldx #0
@t:
  lda edge,x
  and #BTN_START
  beq @tn
  jsr join
  bcc @tn
  lda #1
  sta GSTATE
  sta ROUND
  jsr clr_timer
@tn:
  inx
  cpx #NPLAYERS
  bne @t
  rts

@over:
  jsr inc_timer
  lda TIMERLO
  cmp #180
  bne @od
  jsr reset_players
  lda #0
  sta GSTATE
  sta ROUND
@od:
  rts

@clear:
  jsr inc_timer
  lda TIMERLO
  cmp #120
  bne @cd
  inc ROUND
  lda #1
  sta GSTATE
  jsr clr_timer
@cd:
  rts

@play:
  ldx #0
@p:
  stx cur
  jsr pofs
  lda PBASE+P_STATE,y
  cmp #1
  beq @alive
  cmp #2
  beq @cont
  ; not playing: START joins in
  lda edge,x
  and #BTN_START
  beq @pn
  jsr join
  jmp @pn
@cont:
  jsr countdown
  jmp @pn
@alive:
  jsr move
  ldx cur
  lda edge,x
  and #BTN_A
  beq @nb
  lda #10
  jsr add_score
  ldx cur
  jsr beep
@nb:
  ldx cur
  lda edge,x
  and #BTN_B
  beq @pn
  jsr pofs
  lda PBASE+P_LIVES,y
  beq @pn
  sec
  sbc #1
  sta PBASE+P_LIVES,y
  bne @pn
  jsr game_over_player
@pn:
  ldx cur
  inx
  cpx #NPLAYERS
  bne @p

  ; anyone still in (playing or deciding to continue)?
  ldx #0
  lda #0
  sta tmp0
@any:
  jsr pofs
  lda PBASE+P_STATE,y
  beq @an
  inc tmp0
@an:
  inx
  cpx #NPLAYERS
  bne @any
  lda tmp0
  bne @running
  lda #3
  sta GSTATE
  jsr clr_timer
  rts
@running:
  ; stage boundary every 20 s of play (1200 frames = $04B0)
  jsr inc_timer
  lda TIMERHI
  cmp #$04
  bcc @sd
  lda TIMERLO
  cmp #$B0
  bcc @sd
  lda #4
  sta GSTATE
  jsr clr_timer
@sd:
  rts

; ---- versus (2 players, best of 3 rounds, first to 5 hits)
update_versus:
  jsr coins
  lda GSTATE
  cmp #1
  bne :+
  jmp @round
:
  cmp #2
  bne :+
  jmp @roundover
:
  cmp #3
  bne :+
  jmp @matchover
:
  ; waiting: joins / continues; start when both are in
  ldx #0
@w:
  stx cur
  jsr pofs
  lda PBASE+P_STATE,y
  cmp #1
  beq @wn
  cmp #2
  bne @wjoin
  lda edge,x
  and #BTN_START
  beq @wcd
  jsr join
  jmp @wn
@wcd:
  jsr countdown
  jmp @wn
@wjoin:
  lda edge,x
  and #BTN_START
  beq @wn
  jsr join
@wn:
  ldx cur
  inx
  cpx #2
  bne @w
  lda PBASE+P_STATE
  cmp #1
  bne @wd
  lda PBASE+8+P_STATE
  cmp #1
  bne @wd
  ; start a match
  lda #1
  sta GSTATE
  sta ROUND
  lda #0
  sta PBASE+P_WINS
  sta PBASE+8+P_WINS
  sta HITS
  sta HITS+1
  sta DRAWS
  sta WINNER
  jsr clr_timer
@wd:
  rts

@round:
  ldx #0
@rp:
  stx cur
  jsr move
  ldx cur
  lda edge,x
  and #BTN_A
  beq @rn
  inc HITS,x
  lda #100
  jsr add_score
  ldx cur
  jsr beep
@rn:
  ldx cur
  inx
  cpx #2
  bne @rp
  ; decide the round
  lda #0
  sta tmp0
  lda HITS
  cmp #5
  bcc :+
  lda #1
  sta tmp0
:
  lda HITS+1
  cmp #5
  bcc :+
  lda tmp0
  ora #2
  sta tmp0
:
  lda tmp0
  beq @rd
  cmp #3
  beq @rdraw
  cmp #1
  bne @p2r
  inc PBASE+P_WINS
  jmp @rend
@p2r:
  inc PBASE+8+P_WINS
  jmp @rend
@rdraw:
  inc DRAWS
@rend:
  lda #2
  sta GSTATE
  jsr clr_timer
@rd:
  rts

@roundover:
  jsr inc_timer
  lda TIMERLO
  cmp #90
  bne @rod
  lda #0
  sta HITS
  sta HITS+1
  lda PBASE+P_WINS
  cmp #2
  bcs @m1
  lda PBASE+8+P_WINS
  cmp #2
  bcs @m2
  lda ROUND
  cmp #5
  bcs @mdraw
  inc ROUND
  lda #1
  sta GSTATE
  jsr clr_timer
@rod:
  rts
@m1:
  lda #1
  jmp @mset
@m2:
  lda #2
  jmp @mset
@mdraw:
  lda #$FF
@mset:
  sta WINNER
  inc MATCHID
  lda #3
  sta GSTATE
  jsr clr_timer
  rts

@matchover:
  jsr inc_timer
  lda TIMERLO
  cmp #150
  bne @mod
  ; loser(s) go to the continue countdown, the winner stays in
  lda WINNER
  cmp #1
  beq @l2
  ldx #0
  jsr game_over_player
  lda WINNER
  cmp #$FF
  bne @back
@l2:
  ldx #1
  jsr game_over_player
@back:
  lda #0
  sta GSTATE
  jsr clr_timer
@mod:
  rts

; ---- turns (shared controller: only pad 1 is read)
update_turns:
  lda GSTATE
  cmp #1
  beq @play
  cmp #3
  beq @over
  ; title: SELECT changes participants, START begins
  lda edge
  and #BTN_SEL
  beq @ts
  inc NPART
  lda NPART
  cmp #5
  bcc @ts
  lda #2
  sta NPART
@ts:
  lda edge
  and #BTN_START
  beq @td
  jsr reset_players
  ldx #0
@tj:
  jsr clear_score
  jsr pofs
  lda #1
  sta PBASE+P_STATE,y
  inx
  cpx NPART
  bne @tj
  lda #0
  sta TURNP
  lda #1
  sta ROUND
  sta GSTATE
@td:
  rts

@over:
  jsr inc_timer
  lda TIMERLO
  cmp #180
  bne @od
  jsr reset_players
  lda #0
  sta GSTATE
  sta ROUND
@od:
  rts

@play:
  ; the shared controller drives whoever's turn it is
  lda pad
  ldx TURNP
  sta pad,x
  lda edge
  pha
  lda #0
  sta edge
  pla
  sta edge,x
  stx cur
  jsr move
  ldx cur
  lda edge,x
  and #BTN_A
  beq @nb
  lda #10
  jsr add_score
  ldx cur
  jsr beep
@nb:
  ldx cur
  lda edge,x
  and #BTN_B
  beq @pd
  ; end of turn
  inc TURNS,x
  inx
  cpx NPART
  bcc @nx
  ldx #0
  inc ROUND
@nx:
  stx TURNP
  lda ROUND
  cmp #4
  bcc @pd
  jsr turns_winner
  inc MATCHID
  lda #3
  sta GSTATE
  jsr clr_timer
@pd:
  rts

; highest score wins; a tie for first is a draw ($FF)
turns_winner:
  lda #0
  sta WINNER
  sta tmp0     ; best index
  ldx #1
@l:
  cpx NPART
  bcs @done
  stx cur
  ; compare player X with best (tmp0): 24-bit
  jsr pofs
  sty tmp1
  ldx tmp0
  jsr pofs
  sty tmp2
  ldy tmp1
  ldx tmp2
  lda PBASE+P_SC2,y
  cmp PBASE+P_SC2,x
  bne @cmp
  lda PBASE+P_SC1,y
  cmp PBASE+P_SC1,x
  bne @cmp
  lda PBASE+P_SC0,y
  cmp PBASE+P_SC0,x
  bne @cmp
  ; equal to best: mark tie
  lda #$FF
  sta WINNER
  jmp @next
@cmp:
  bcc @next
  lda cur
  sta tmp0
  lda #0
  sta WINNER
@next:
  ldx cur
  inx
  jmp @l
@done:
  lda WINNER
  cmp #$FF
  beq @r
  ldx tmp0
  inx
  stx WINNER
@r:
  rts

; ---------------------------------------------------------------- text
; put zero-terminated string at ptr into TEXTBUF+X
puts:
  ldy #0
@l:
  lda (ptr),y
  beq @d
  sta TEXTBUF,x
  inx
  iny
  bne @l
@d:
  rts

.macro PUTS addr, col
  lda #<addr
  sta ptr
  lda #>addr
  sta ptr+1
  ldx #col
  jsr puts
.endmacro

; print A as one decimal digit at TEXTBUF+X
putdigit:
  clc
  adc #'0'
  sta TEXTBUF,x
  inx
  rts

; print 24-bit num0 as 6 digits at TEXTBUF+X
putnum6:
  stx tmp3
  ldy #0
@dig:
  lda #0
  sta digits,y
@sub:
  ; num0 -= pow10[y] while num0 >= pow10[y]
  lda num0
  sec
  sbc p10lo,y
  sta tmp0
  lda num0+1
  sbc p10mi,y
  sta tmp1
  lda num0+2
  sbc p10hi,y
  bcc @nd
  sta num0+2
  lda tmp1
  sta num0+1
  lda tmp0
  sta num0
  lda digits,y
  clc
  adc #1
  sta digits,y
  jmp @sub
@nd:
  iny
  cpy #6
  bne @dig
  ldx tmp3
  ldy #0
@out:
  lda digits,y
  clc
  adc #'0'
  sta TEXTBUF,x
  inx
  iny
  cpy #6
  bne @out
  rts

; player X status at TEXTBUF+A: "P1 012345 L3" (12 chars)
putplayer:
  sta tmp2
  stx cur
  tax
  lda #'P'
  sta TEXTBUF,x
  inx
  lda cur
  clc
  adc #'1'
  sta TEXTBUF,x
  inx
  inx
  stx tmp2
  ldx cur
  jsr pofs
  lda PBASE+P_SC0,y
  sta num0
  lda PBASE+P_SC1,y
  sta num0+1
  lda PBASE+P_SC2,y
  sta num0+2
  ldx tmp2
  jsr putnum6
  inx
  stx tmp2
  ldx cur
  jsr pofs
  ldx tmp2
  lda PBASE+P_STATE,y
  cmp #1
  bne @np
.if MODE_VAL = 0
  lda #'W'
  sta TEXTBUF,x
  inx
  lda PBASE+P_WINS,y
  jsr putdigit
.elseif MODE_VAL = 2
  lda #'T'
  sta TEXTBUF,x
  inx
  ldy cur
  lda TURNS,y
  jsr putdigit
.else
  lda #'L'
  sta TEXTBUF,x
  inx
  lda PBASE+P_LIVES,y
  jsr putdigit
.endif
  rts
@np:
  cmp #2
  bne @none
  lda #'C'
  sta TEXTBUF,x
  inx
  ldy cur
  lda CDOWN,y
  cmp #10
  bcc :+
  lda #9
:
  jsr putdigit
  rts
@none:
  lda #'-'
  sta TEXTBUF,x
  sta TEXTBUF+1,x
  rts

build_text:
  lda #' '
  ldx #0
@clr:
  sta TEXTBUF,x
  inx
  bpl @clr
  ; row 0: title, credits, round/stage
.if MODE_VAL = 0
  PUTS s_versus, 1
.elseif MODE_VAL = 1
  PUTS s_coop, 1
.elseif MODE_VAL = 2
  PUTS s_turns, 1
.else
  PUTS s_solo, 1
.endif
.if MODE_VAL = 2
  PUTS s_players, 14
  lda NPART
  ldx #22
  jsr putdigit
.else
  PUTS s_credit, 14
  lda CREDITS
  ldx #21
  jsr putdigit
.endif
  PUTS s_rnd, 24
  lda ROUND
  cmp #10
  bcc :+
  lda #9
:
  ldx #28
  jsr putdigit

  ; rows 1-2: players
  ldx #0
  lda #32+1
  jsr putplayer
.if NPLAYERS > 1
  ldx #1
  lda #32+17
  jsr putplayer
.endif
.if NPLAYERS > 2
  ldx #2
  lda #64+1
  jsr putplayer
  ldx #3
  lda #64+17
  jsr putplayer
.endif

  ; row 3: message
  lda GSTATE
  bne @m1
.if MODE_VAL = 2
  PUTS s_selstart, 96+4
.else
  lda PBASE+P_STATE
  cmp #1
  beq @waitc
  lda PBASE+8+P_STATE
  cmp #1
  beq @waitc
  PUTS s_insert, 96+3
  rts
@waitc:
  PUTS s_challenger, 96+3
.endif
  rts
@m1:
  cmp #1
  bne @m2
.if MODE_VAL = 2
  PUTS s_turnof, 96+8
  lda TURNP
  clc
  adc #1
  ldx #96+16
  jsr putdigit
.endif
  rts
@m2:
  cmp #2
  bne @m3
  PUTS s_roundover, 96+9
  rts
@m3:
  cmp #3
  bne @m4
.if MODE_VAL = 0 || MODE_VAL = 2
  lda WINNER
  cmp #$FF
  bne @win
  PUTS s_draw, 96+12
  rts
@win:
  PUTS s_wins, 96+10
  lda WINNER
  ldx #96+11
  jsr putdigit
.else
  PUTS s_gameover, 96+11
.endif
  rts
@m4:
  PUTS s_stageclear, 96+10
  rts

; ---------------------------------------------------------------- sprites
build_sprites:
  ldx #0
@l:
  stx cur
  txa
  asl
  asl
  tax            ; X = OAM offset
  ldy cur
  lda #$FF
  sta OAMBUF,x   ; hidden by default
  tya
  asl
  asl
  asl
  tay            ; Y = player block
  lda PBASE+P_STATE,y
  cmp #1
  bne @n
  lda PBASE+P_Y,y
  sta OAMBUF,x
  lda #$80
  sta OAMBUF+1,x
  lda cur
  sta OAMBUF+2,x
  lda PBASE+P_X,y
  sta OAMBUF+3,x
@n:
  ldx cur
  inx
  cpx #4
  bne @l
  rts

; ---------------------------------------------------------------- data
.segment "RODATA"
palette:
  .byte $0F,$30,$10,$00, $0F,$30,$10,$00, $0F,$30,$10,$00, $0F,$30,$10,$00
  .byte $0F,$16,$26,$36, $0F,$12,$22,$32, $0F,$1A,$2A,$3A, $0F,$28,$38,$30
start_x:  .byte 48, 192, 96, 144
beep_lo:  .byte $FD, $A9, $7E, $54
p10lo:    .byte .lobyte(100000), .lobyte(10000), .lobyte(1000), .lobyte(100), .lobyte(10), .lobyte(1)
p10mi:    .byte .hibyte(100000), .hibyte(10000), .hibyte(1000), .hibyte(100), .hibyte(10), .hibyte(1)
p10hi:    .byte .bankbyte(100000), .bankbyte(10000), .bankbyte(1000), .bankbyte(100), .bankbyte(10), .bankbyte(1)
s_versus: .asciiz "ATC VERSUS"
s_coop:   .asciiz "ATC CO-OP"
s_turns:  .asciiz "ATC TURNS"
s_solo:   .asciiz "ATC SOLO"
s_credit: .asciiz "CREDIT"
s_players:.asciiz "PLAYERS"
s_rnd:    .asciiz "RND"
s_insert: .asciiz "INSERT COIN  PRESS START"
s_challenger: .asciiz "WAITING FOR CHALLENGER"
s_selstart: .asciiz "SELECT PLAYERS  START"
s_turnof: .asciiz "TURN OF P"
s_roundover: .asciiz "ROUND OVER"
s_draw:   .asciiz "DRAW"
s_wins:   .asciiz "P  WINS"
s_gameover: .asciiz "GAME OVER"
s_stageclear: .asciiz "STAGE CLEAR"

.segment "VECTORS"
  .word nmi, reset, irq

.segment "CHARS"
  .incbin "build/font.chr"
