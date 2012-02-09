/*
 * Copyright (c) 2011 Alexey Illarionov <littlesavage@rambler.ru>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/***********************************************************************************
*	Copyright 2005 Anglia Design
*	This demo code and associated components are provided as is and has no warranty,
*	implied or otherwise.  You are free to use/modify any of the provided
*	code at your own risk in your applications with the expressed limitation
*	of liability (see below)
*
*	LIMITATION OF LIABILITY:   ANGLIA OR ANGLIA DESIGNS SHALL NOT BE LIABLE FOR ANY
*	LOSS OF PROFITS, LOSS OF USE, LOSS OF DATA, INTERRUPTION OF BUSINESS, NOR FOR
*	INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES OF ANY KIND WHETHER UNDER
*	THIS AGREEMENT OR OTHERWISE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
*
*	Author			: Spencer Oliver
*	Web     		: www.anglia-designs.com
*
***********************************************************************************/

/* Stack Sizes */

	_HEAPSIZE = 1024;

/* Memory Definitions */

MEMORY
{
	DATA (rw) : ORIGIN = 0x0, LENGTH = 0x2400
	XDATA (rw) : ORIGIN = 0x2400, LENGTH = 0x4000
}

__stack_end__ = 0x4000;

/* Section Definitions */

SECTIONS
{
	/* first section is .text which is used for code */

        .startup : 
        {
                KEEP(*(.startup))
        } > XDATA =0

	.text :
	{
		KEEP(*(.init))
		*(.text .text.*)
		*(.gnu.linkonce.t.*)
		*(.glue_7t .glue_7)
		KEEP(*(.fini))
		*(.gcc_except_table)
	} >XDATA
	. = ALIGN(4);

	/* .rodata section which is used for read-only data (constants) */
	.rodata :
	{
		*(.rodata .rodata.*)
		*(.gnu.linkonce.r.*)
	} >XDATA
	. = ALIGN(4);


	_etext = .;
	PROVIDE (etext = .);

	/* .data section which is used for initialized data */
	.data : AT (_etext)
	{
		*(.data .data.*)
		*(.gnu.linkonce.d.*)
		SORT(CONSTRUCTORS)
		*(.program_version)

	} >XDATA
	. = ALIGN(4);

	__data_start = .;
	_edata = .;
	PROVIDE (edata = .);

	/* .bss section which is used for uninitialized data */

	.bss :
	{
		__bss_start = .;
		__bss_start__ = .;
		*(.bss .bss.*)
		*(.gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(4);
	} >XDATA
	. = ALIGN(4);
	__bss_end__ = .;

	_end = .;
	PROVIDE(end = .);

	/* .heap section which is used for memory allocation */

	.heap (NOLOAD) :
	{
		__heap_start__ = .;
		*(.heap)
		. = MAX(__heap_start__ + _HEAPSIZE , .);
	} >DATA
	__heap_end__ = __heap_start__ + SIZEOF(.heap);
	/* .stack section - user mode stack */


	.stack (__heap_end__ + 3) / 4 * 4 (NOLOAD) :
	{
	   __stack_start__ = .;
	   *(.stack)
	   . = __stack_end__;
	} >XDATA

}

