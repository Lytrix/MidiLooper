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
		*RevisionLoad*.o(.text .text.*)
		*LoopInternalColdHelpers*.o(.text .text.*)
		*SetBrowserRead*.o(.text .text.*)
		*StorageManagerHitlSerial*.o(.text .text.*)
		*DebugSessionCapture*.o(.text .text.*)
		*NoteEditSessionUndoStack*.o(.text .text.*)
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
