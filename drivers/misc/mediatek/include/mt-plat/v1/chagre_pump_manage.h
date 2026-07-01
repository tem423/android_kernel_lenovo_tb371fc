#ifndef __PD_POLICY_MANAGER_H__
#define __PD_POLICY_MANAGER_H__



extern int sgm41600_probe(struct i2c_client *client,
			 const struct i2c_device_id *id);
extern void sgm41600_charger_shutdown(struct i2c_client *client);
extern int sgm41600_charger_remove(struct i2c_client *client);
extern int sgm41600_suspend_noirq(struct device *dev);
extern int sgm41600_suspend(struct device *dev);
extern int sgm41600_resume(struct device *dev);
extern int sgm41600_hw_chipid_detect(struct i2c_client *client);

extern int sc854x_charger_probe(struct i2c_client *client,
                    const struct i2c_device_id *id);
extern int sc854x_charger_remove(struct i2c_client *client);
extern int sc854x_resume(struct device *dev);
extern int sc854x_suspend_noirq(struct device *dev);
extern int sc854x_suspend(struct device *dev);
extern void sc854x_charger_shutdown(struct i2c_client *client);
extern int sc854x_hw_chipid_detect(struct i2c_client *client);

#endif