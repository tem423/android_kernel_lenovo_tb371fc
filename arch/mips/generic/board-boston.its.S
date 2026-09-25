/ {
	images {
<<<<<<< HEAD
		fdt@boston {
=======
		fdt-boston {
>>>>>>> origin/android16-base
			description = "img,boston Device Tree";
			data = /incbin/("boot/dts/img/boston.dtb");
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
		conf@boston {
			description = "Boston Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@boston";
=======
		conf-boston {
			description = "Boston Linux kernel";
			kernel = "kernel";
			fdt = "fdt-boston";
>>>>>>> origin/android16-base
		};
	};
};
