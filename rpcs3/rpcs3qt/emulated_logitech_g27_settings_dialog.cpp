#include "stdafx.h"

#include "emulated_logitech_g27_settings_dialog.h"

#include "Emu/Io/LogitechG27.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

enum button_role
{
	button = Qt::UserRole,
	emulated_button
};

emulated_logitech_g27_settings_dialog::emulated_logitech_g27_settings_dialog(QWidget* parent)
	: QDialog(parent)
{
	setObjectName("emulated_logitech_g27_settings_dialog");
	setWindowTitle(tr("Configure Emulated Logitech G27 Wheel"));
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_StyledBackground);
	setModal(true);

	QVBoxLayout* v_layout = new QVBoxLayout(this);

	QDialogButtonBox* buttons = new QDialogButtonBox(this);
	buttons->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel | QDialogButtonBox::Save | QDialogButtonBox::RestoreDefaults);

	g_cfg_logitech_g27.load();

	// TODO create UI elements
	// TODO load UI states from config

	connect(buttons, &QDialogButtonBox::clicked, this, [this, buttons](QAbstractButton* button)
	{
		if (button == buttons->button(QDialogButtonBox::Apply))
		{
			// TODO apply ui state to config
			g_cfg_logitech_g27.save();
			// TODO reload UI states from config
		}
		else if (button == buttons->button(QDialogButtonBox::Save))
		{
			// TODO apply ui state to config
			g_cfg_logitech_g27.save();
			accept();
		}
		else if (button == buttons->button(QDialogButtonBox::RestoreDefaults))
		{
			if (QMessageBox::question(this, tr("Confirm Reset"), tr("Reset all buttons?")) != QMessageBox::Yes)
				return;
			g_cfg_logitech_g27.fill_defaults();
			// TODO reload UI states from config
		}
		else if (button == buttons->button(QDialogButtonBox::Cancel))
		{
			reject();
		}
	});

	v_layout->addWidget(buttons);
	setLayout(v_layout);
}
