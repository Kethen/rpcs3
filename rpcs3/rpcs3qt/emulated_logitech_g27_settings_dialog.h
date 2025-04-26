#pragma once

#include <QComboBox>
#include <QDialog>
#include <QTabWidget>

#include <vector>

class emulated_logitech_g27_settings_dialog : public QDialog
{
	Q_OBJECT

public:
	emulated_logitech_g27_settings_dialog(QWidget* parent = nullptr);

private:
	void load_config();
	void save_config();
	void reset_config();

	std::vector<std::vector<QComboBox*>> m_combos;
};
