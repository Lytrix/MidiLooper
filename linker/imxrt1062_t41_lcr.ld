/* Teensy 4.1 linker script — imxrt1062_t41.ld + LoopContentResolution STL in FLASH.
 *
 * LoopContentResolution.cpp: TRACK_COLD_MEM covers named functions; STL template
 * bodies land in per-symbol .text.* and default to ITCM. Route that object file's
 * .text* to FLASH so flexRAM ITCM banks are not crossed.
 * LoopSnapshot.cpp / LoopSnapshotPublish.cpp: same STL/shared_ptr template leak.
 * Display cache/window/note-edit projection TUs: stop-path and paint helpers leak
 * STL .text.* into ITCM; route them with InvestigationCaptureLog.
 */
MEMORY
{
	ITCM (rwx):  ORIGIN = 0x00000000, LENGTH = 512K
	DTCM (rwx):  ORIGIN = 0x20000000, LENGTH = 512K
	RAM (rwx):   ORIGIN = 0x20200000, LENGTH = 512K
	FLASH (rwx): ORIGIN = 0x60000000, LENGTH = 7936K
	ERAM (rwx):  ORIGIN = 0x70000000, LENGTH = 16384K
}

ENTRY(ImageVectorTable)

SECTIONS
{
	.text.headers : {
		KEEP(*(.flashconfig))
		FILL(0xFF)
		. = ORIGIN(FLASH) + 0x1000;
		KEEP(*(.ivt))
		KEEP(*(.bootdata))
		. = ALIGN(1024);
	} > FLASH

	.text.code : {
		KEEP(*(.startup))
		*(.flashmem*)
		*LoopContentResolution*.o(.text .text.*)
		*LoopOverdubSession*.o(.text .text.*)
		*LoopSnapshot*.o(.text .text.*)
		*TrackOverdubWrap*.o(.text .text.*)
		/* Deferred maintenance runs from the main-loop owner, never the MIDI
		   ISR. Keep its diagnostics and continuation helpers in FLASH so adding
		   a recording-window service cannot cross another ITCM flexRAM bank. */
		*TrackDeferredMaintenance*.o(.text .text.*)
		/* The complete compatibility order builder is FLASHMEM. Keep its
		   instantiated sort helpers beside it instead of spending an ITCM
		   bank on the implementation being retired by bounded playback. */
		*TrackPlaybackWindowBuild.cpp.o(.text.*rebuildPlaybackOrder*)
		/* Create's complete reference sorter is cold too. Its generated
		   stable-sort helpers do not inherit the caller's FLASHMEM section. */
		*EditApply.cpp.o(.text.*applyAddNote*)
		*TrackUndo*.o(.text .text.*)
		*InvestigationCaptureLog*.o(.text .text.*)
		*DisplayCacheLifecycle*.o(.text .text.*)
		*DisplayNoteWindowGather*.o(.text .text.*)
		*DisplayNoteResolveCommitted*.o(.text .text.*)
		*DisplayNoteResolveLiveCapture*.o(.text .text.*)
		*DisplayWindowUtils*.o(.text .text.*)
		*PianoRollDraw*.o(.text .text.*)
		*NoteEditFocusDisplayProjection*.o(.text .text.*)
		*NoteEditFocusLinearSpan*.o(.text .text.*)
		/* Boot/load and SD-bundle serialization only -- never the MIDI tick, the
		   capture append, or playback serving. These six TUs carry no COLD/FLASH
		   macro, so all of their code defaulted to ITCM: moving them frees two
		   flexRAM ITCM banks (72 KB measured), which hands DTCM the banks back as
		   stack headroom. Without it the tree sat one 4 KB alignment unit from
		   `locals free` 2,240, where a few bytes of new code hung the boot load
		   (2026-09-18). `_itcm_block_count` drops 13 -> 11. */
		*LoadLoopJob*.o(.text .text.*)
		*CurrentSetBootLoad*.o(.text .text.*)
		*RevisionCommit*.o(.text .text.*)
		*CurrentWorkspaceStorage*.o(.text .text.*)
		/* The deferred workspace bundle stages are cold SD serialization (footer/undo
		   headers), never the MIDI tick or capture append. The one-header-per-slice
		   split added 256 B of non-FLASHMEM code to this TU and crossed the ITCM
		   padding floor; keep the whole TU in FLASH. */
		*DeferredSaveJobStages*.o(.text .text.*)
		*RevisionLoad*.o(.text .text.*)
		*LoopInternalColdHelpers*.o(.text .text.*)
		*SetBrowserRead*.o(.text .text.*)
		*StorageManagerHitlSerial*.o(.text .text.*)
		*RevisionStoreProbe*.o(.text .text.*)
		*DebugSessionCapture*.o(.text .text.*)
		*NoteEditSessionUndoStack*.o(.text .text.*)
		/* The DIN *input* path: the `MidiInterface` parse pipeline this TU
		   instantiates is entered from `MidiHandler::handleMidiInput`, which
		   only the main loop calls. `send` deliberately stays in ITCM -- the
		   MIDI tick ISR reaches it through `sendClock`. The rule is the one
		   DroidHostFeedbackRing.h cites: what the tick ISR can reach stays in
		   ITCM, everything else is FLASH headroom. */
		*MidiHandler.cpp.o(.text._ZN4midi*5parseEv .text._ZN4midi*14launchCallbackEv .text._ZN4midi*10thruFilterEh)
		. = ALIGN(4);
		KEEP(*(.init))
		__preinit_array_start = .;
		KEEP (*(.preinit_array))
		__preinit_array_end = .;
		__init_array_start = .;
		KEEP (*(.init_array))
		__init_array_end = .;
		. = ALIGN(4);
	} > FLASH

	.text.progmem : {
		*(.progmem*)
		/* Persist telemetry strings: .rodata is DTCM in this script and
		   the extra ~1 KB from LoopSnapshotTail labels crosses the 4K
		   _VectorsRam/periodictable align edge (+4 KB RAM1 locals). */
		*WorkspaceSave*.o(.rodata .rodata.*)
		/* Capture-admission diagnostics otherwise push DTCM across the
		   vectors/USB periodic-table alignment edge, costing 4 KB of stack
		   headroom. Keep only these string literals in memory-mapped FLASH;
		   mutable state and non-string capture lookup data retain their placement. */
		*TrackCaptureInput.cpp.o(.rodata.str* .rodata.*.str*)
		*TrackOverdubLifecycle.cpp.o(.rodata.str* .rodata.*.str*)
		/* DEC-233's overdub-press / stop-latched button rows added ~190 B of string
		   literals to MidiButtonActions.cpp, which had no rodata carve-out; they
		   crossed the same _VectorsRam/periodictable align edge. Only read-only
		   literals move. */
		*MidiButtonActions*.o(.rodata.str* .rodata.*.str*)
		/* windowed-playback-generation slice 3.5: the readiness-holder names are the
		   same shape as the two entries above -- their ~100 B crossed the
		   _VectorsRam/periodictable align edge and cost 4 KB of RAM1 locals
		   (variables 96,192 -> 100,288 with ITCM code byte-identical). Only the
		   string literals move, so the row strings this TU already carries travel
		   with them; all of them are read-only. */
		/* windowed-playback-generation slice 3.9: the same edge, one class wider. The
		   readiness-**label** names added ~100 B, but what crossed the align edge was the
		   compiler's own **jump tables** for the readiness chains (`UNMATCHED:
		   .rodata.CSWTCH.377/476/494` -- the if/else form does not stop the optimizer from
		   tabulating a dense enum chain). All of these are read-only, so they travel with the
		   string literals slice 3.5 moved: `variables` 100,288 -> 96,192 with the ITCM
		   `code` figure byte-identical. */
		*TrackPlaybackWindowBuild.cpp.o(.rodata.str* .rodata.*.str* .rodata.CSWTCH*)
		/* Timing-path review, item 1: the tick edge's deferred results carry one new
		   string (the drop report), and ~90 B of DTCM string data was enough to cross
		   the same align edge the four entries above record -- `variables` 96,192 ->
		   100,288 with ITCM code byte-identical. This TU's row strings are the
		   "LoopEnd playback commit ..." evidence the host tooling greps for, and they
		   travel with the new one; all of them are read-only. */
		*TrackManagerTransportTick.cpp.o(.rodata.str* .rodata.*.str*)
		/* S7.5a distribution instrument: the two rows that carry a window's own p50/p99 intervals and its per-route
		   lateness split. Their format strings and channel names are ~130 B of new string literals, and adding them to
		   `DebugSessionCapture.cpp` crossed this same align edge (`variables` 96,192 -> 100,288, locals 34,880 ->
		   30,784, ITCM code unchanged: code 392,440 + padding 776, padding below the 840 floor). Rather than re-base
		   the budget for a 4 KB align step, the rows moved into their own TU, which is carved out here: the literals
		   stay in memory-mapped FLASH and the approved budget is untouched. Read-only. */
		*EnvelopeDistRows.cpp.o(.rodata.str* .rodata.*.str*)
		/* Task 6.4 display-window transition receipt: `DisplayNoteWindowGather.cpp` hosts the window owner's sampler, and
		   that TU's ~140 B of read-only strings cross the `_VectorsRam`/periodictable align edge -- `variables` 96,192 ->
		   100,288 (measured by removing this line). Only read-only literals move: the sampler's mutable state is `.bss.dma`
		   (external RAM, `DISP_RECEIPT_MEM`), so invariant 36 holds and no query of this carve-out can hide DTCM state.
		   (A sampler call in `update()` would also have spent 16 B of ITCM -- exactly the qualification image's padding
		   above the floor -- so the call rides the existing capture hook instead.) */
		*DisplayNoteWindowGather*.o(.rodata .rodata.*)
		/* CONTRACTS.md C3 (P-B): the private-predecessor source origin's own tokens
		   ("private_predecessor", "none", the five denial names) are ~100 B of new read-only string
		   literals, and they cross the same `_VectorsRam`/periodictable align edge the entries above
		   record -- `variables` 96,192 -> 100,288 with ITCM `code` byte-identical (measured by building
		   without this line). Only literals move: this TU owns no mutable state (the cursor lives in
		   `Loop`, which is externally allocated), so invariant 36 holds. */
		*LoopPrivateSource.cpp.o(.rodata.str* .rodata.*.str*)
		/* The same P-B change carries one log format literal and one short token argument in
		   `TrackDeferredMaintenance.cpp`, whose `.rodata.str*` had no carve-out: the pair crosses the
		   same align edge on its own (measured by removing both this line and the one above:
		   `variables` 100,288). Read-only.
		   NOTE: this is a whole-TU pattern, not one row's names -- every future string literal added to
		   TrackDeferredMaintenance.cpp leaves DTCM and lands in FLASH *unmeasured* for the RAM1 budget.
		   A future change that needs that TU's literals back in DTCM must remove this row and re-base
		   `scripts/ram1_budget.json`, not merely edit a comment. */
		*TrackDeferredMaintenance.cpp.o(.rodata.str* .rodata.*.str*)
		/* DEC-186 (2026-10-28): the companion-preparation refusal names are ~450 B of new read-only
		   literals in `LoopPreparedCompanionHistory.cpp`, and they cross the same
		   `_VectorsRam`/periodictable align edge as the entries above: `variables` 96,192 -> 100,288
		   (+4,096 B, ITCM `code` byte-identical) measured by building without this line. The TU owns no
		   mutable state (the horizon lives in externally allocated `Loop`), so only literals move and
		   invariant 36 holds. Same whole-TU caveat as the row above: a future change that needs this
		   TU's literals back in DTCM must remove this row and re-base `scripts/ram1_budget.json`. */
		*LoopPreparedCompanionHistory.cpp.o(.rodata.str* .rodata.*.str*)
		/* Task 3.1 persistence named-step row: the new format literals in
		   `PersistenceDiagnostics.cpp` cross the same `_VectorsRam`/periodictable align edge
		   (`variables` 96,192 -> 100,288 measured by removing this line; ITCM +128 B for the
		   cold `onPersistenceStep` function, padding 968 >= 840). Only read-only literals move;
		   the rate-limit array is DMAMEM, so no mutable state is hidden here. */
		*PersistenceDiagnostics.cpp.o(.rodata.str* .rodata.*.str*)
		/* Deferred-stop tail liveness (`DIAG,stop_tail,`) and the aggregate owner-turn row
		   (`DIAG,idle_turn,`): their Tier-A prefixes live in `StopDiagRows.cpp`, because one more
		   16-byte literal in `DebugSessionCapture.cpp`'s classifier or in `CaptureLineTier.h` crossed
		   the same `_VectorsRam`/periodictable align edge (`variables` 96,192 -> 100,288 measured by
		   removing this line; ITCM `code` byte-identical). Prefixes moved here instead of re-basing the
		   budget. Read-only; the TU owns no mutable state. */
		*StopDiagRows.cpp.o(.rodata.str* .rodata.*.str*)
		/* Runtime profiler names, row formats and command strings (DEC-239,
		   openspec/changes/runtime-profiling-layer), plus the compiler's switch tables for the
		   name and ceiling lookups (`.rodata.CSWTCH.*`, 56-60 B each, measured in the profile ELF):
		   the slice-3.9 lesson above -- a dense enum switch is tabulated even in a FLASHMEM function.
		   Compiled only with RUNTIME_PROFILE_SCOPES; the rules match no object in other envs.
		   Read-only; neither TU owns mutable data here (the profiler state is DMAMEM). */
		*ProfileDiagRows.cpp.o(.rodata.str* .rodata.*.str* .rodata.CSWTCH*)
		*RuntimeProfiler.cpp.o(.rodata.str* .rodata.*.str* .rodata.CSWTCH*)
		. = ALIGN(4);
	} > FLASH

	.text.itcm : {
		. = . + 32; /* MPU to trap NULL pointer deref */
		*(.fastrun)
		EXCLUDE_FILE (*LoopContentResolution* *LoopOverdubSession* *LoopSnapshot* *TrackOverdubWrap* *TrackUndo*) *(.text*)
		. = ALIGN(16);
	} > ITCM  AT> FLASH

	.ARM.exidx : {
		__exidx_start = .;
		*(.ARM.exidx* .ARM.extab.text* .gnu.linkonce.armexidx.*)
		__exidx_end = .;
	} > ITCM  AT> FLASH

	.data : {
		*(.endpoint_queue)   
		EXCLUDE_FILE (*WorkspaceSave*) *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.data*)))
		KEEP(*(.vectorsram))
	} > DTCM  AT> FLASH

	.bss ALIGN(4) : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(COMMON)))
		. = ALIGN(32);
		. = . + 32; /* MPU to trap stack overflow */
	} > DTCM

	.bss.dma (NOLOAD) : {
		*(.hab_log)
		*(.dmabuffers)
		. = ALIGN(32);
	} > RAM

	.bss.extram (NOLOAD) : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.externalram)))
		. = ALIGN(32);
	} > ERAM

	.text.csf : {
		FILL(0xFF)
		. = ALIGN(1024);
		KEEP(*(.csf))
		__text_csf_end = .;
	} > FLASH

	_stext = ADDR(.text.itcm);
	_etext = ADDR(.text.itcm) + SIZEOF(.text.itcm) + SIZEOF(.ARM.exidx);
	_stextload = LOADADDR(.text.itcm);

	_sdata = ADDR(.data);
	_edata = ADDR(.data) + SIZEOF(.data);
	_sdataload = LOADADDR(.data);

	_sbss = ADDR(.bss);
	_ebss = ADDR(.bss) + SIZEOF(.bss);

	_heap_start = ADDR(.bss.dma) + SIZEOF(.bss.dma);
	_heap_end = ORIGIN(RAM) + LENGTH(RAM);

	_extram_start = ADDR(.bss.extram);
	_extram_end = ADDR(.bss.extram) + SIZEOF(.bss.extram);

	_itcm_block_count = (SIZEOF(.text.itcm) + SIZEOF(.ARM.exidx) + 0x7FFF) >> 15;
	_flexram_bank_config = 0xAAAAAAAA | ((1 << (_itcm_block_count * 2)) - 1);
	_estack = ORIGIN(DTCM) + ((16 - _itcm_block_count) << 15);

	_flashimagelen = __text_csf_end - ORIGIN(FLASH);
	_teensy_model_identifier = 0x25;

	.debug_info     0 : { *(.debug_info) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line) }
	.debug_frame    0 : { *(.debug_frame) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }

}
