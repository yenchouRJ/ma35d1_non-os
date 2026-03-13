;/*---------------------------------------------------------------------------------------------------------*/
;/*                                                                                                         */
;/* Copyright(c) 2023 Nuvoton Technology Corp. All rights reserved.                                         */
;/*                                                                                                         */
;/*---------------------------------------------------------------------------------------------------------*/

;//	.syntax	unified

	.globl	ImageDataBase
	.globl	ImageDataLimit

	.align	4

	.text

ImageDataBase:
	.incbin "../../myimage_1280x800.raw"

ImageDataLimit:
	.space   4

	.end

