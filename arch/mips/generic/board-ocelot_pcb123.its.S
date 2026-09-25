/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/ {
	images {
<<<<<<< HEAD
		fdt@ocelot_pcb123 {
=======
		fdt-ocelot_pcb123 {
>>>>>>> origin/android16-base
			description = "MSCC Ocelot PCB123 Device Tree";
			data = /incbin/("boot/dts/mscc/ocelot_pcb123.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
<<<<<<< HEAD
			hash@0 {
=======
			hash {
>>>>>>> origin/android16-base
				algo = "sha1";
			};
		};
	};

	configurations {
<<<<<<< HEAD
		conf@ocelot_pcb123 {
			description = "Ocelot Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@ocelot_pcb123";
=======
		conf-ocelot_pcb123 {
			description = "Ocelot Linux kernel";
			kernel = "kernel";
			fdt = "fdt-ocelot_pcb123";
>>>>>>> origin/android16-base
		};
	};
};
