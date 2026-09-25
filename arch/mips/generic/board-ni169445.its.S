/ {
	images {
<<<<<<< HEAD
		fdt@ni169445 {
=======
		fdt-ni169445 {
>>>>>>> origin/android16-base
			description = "NI 169445 device tree";
			data = /incbin/("boot/dts/ni/169445.dtb");
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
		conf@ni169445 {
			description = "NI 169445 Linux Kernel";
			kernel = "kernel@0";
			fdt = "fdt@ni169445";
=======
		conf-ni169445 {
			description = "NI 169445 Linux Kernel";
			kernel = "kernel";
			fdt = "fdt-ni169445";
>>>>>>> origin/android16-base
		};
	};
};
