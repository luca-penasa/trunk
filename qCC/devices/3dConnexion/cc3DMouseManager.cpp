//##########################################################################
//#                                                                        #
//#                              CLOUDCOMPARE                              #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 or later of the License.      #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: CloudCompare project                               #
//#                                                                        #
//##########################################################################

#include <QAction>
#include <QMainWindow>
#include <QMenu>

#include "cc3DMouseManager.h"
#include "ccGLWindow.h"

#include "ccMainAppInterface.h"

#include "devices/3dConnexion/Mouse3DInput.h"


cc3DMouseManager::cc3DMouseManager( ccMainAppInterface *appInterface, QObject *parent ) :
    QObject( parent ),
    m_appInterface( appInterface ),
    m3dMouseInput( nullptr )
{
    setupMenu();

    enableDevice(true, true);
}

cc3DMouseManager::~cc3DMouseManager()
{
    releaseDevice();

	if (m_menu)
	{
		delete m_menu;
	}
}

void cc3DMouseManager::enableDevice(bool state, bool silent)
{
	if (m3dMouseInput)
	{
		releaseDevice();
	}

	if (state)
	{
		m3dMouseInput = new Mouse3DInput(this);
		if (m3dMouseInput->connect(m_appInterface->getMainWindow(),"CloudCompare"))
		{
			connect(m3dMouseInput, &Mouse3DInput::sigMove3d, this, &cc3DMouseManager::on3DMouseMove);
			connect(m3dMouseInput, &Mouse3DInput::sigReleased, this, &cc3DMouseManager::on3DMouseReleased);
			connect(m3dMouseInput, &Mouse3DInput::sigOn3dmouseKeyDown, this, &cc3DMouseManager::on3DMouseKeyDown);
			connect(m3dMouseInput, &Mouse3DInput::sigOn3dmouseKeyUp, this, &cc3DMouseManager::on3DMouseKeyUp);
		}
		else
		{
			delete m3dMouseInput;
			m3dMouseInput = nullptr;

			if (!silent)
			{
				ccLog::Error("[3D Mouse] No device found"); //warning message has already been issued by Mouse3DInput::connect
			}
			state = false;
		}
	}
	else
	{
		ccLog::Warning("[3D Mouse] Device has been disabled");
	}

	m_actionEnable->blockSignals(true);
	m_actionEnable->setChecked(state);
	m_actionEnable->blockSignals(false);
}

void cc3DMouseManager::releaseDevice()
{
	if (m3dMouseInput == nullptr)
		return;

	m3dMouseInput->disconnectDriver(); //disconnect from the driver
	m3dMouseInput->disconnect(this); //disconnect from Qt ;)

	delete m3dMouseInput;
	m3dMouseInput = nullptr;
}

void cc3DMouseManager::on3DMouseKeyUp(int)
{
	//nothing right now
}

// ANY CHANGE/BUG FIX SHOULD BE REFLECTED TO THE EQUIVALENT METHODS IN QCC "MainWindow.cpp" FILE!
void cc3DMouseManager::on3DMouseKeyDown(int key)
{
	switch(key)
	{
		case Mouse3DInput::V3DK_MENU:
			//should be handled by the driver now!
			break;
		case Mouse3DInput::V3DK_FIT:
		{
			if (m_appInterface->getSelectedEntities().empty())
			{
				m_appInterface->setGlobalZoom();
			}
			else
			{
				m_appInterface->zoomOnSelectedEntities();
			}
		}
			break;
		case Mouse3DInput::V3DK_TOP:
			m_appInterface->setTopView();
			break;
		case Mouse3DInput::V3DK_LEFT:
			m_appInterface->setLeftView();
			break;
		case Mouse3DInput::V3DK_RIGHT:
			m_appInterface->setRightView();
			break;
		case Mouse3DInput::V3DK_FRONT:
			m_appInterface->setFrontView();
			break;
		case Mouse3DInput::V3DK_BOTTOM:
			m_appInterface->setBottomView();
			break;
		case Mouse3DInput::V3DK_BACK:
			m_appInterface->setBackView();
			break;
		case Mouse3DInput::V3DK_ROTATE:
			//should be handled by the driver now!
			break;
		case Mouse3DInput::V3DK_PANZOOM:
			//should be handled by the driver now!
			break;
		case Mouse3DInput::V3DK_ISO1:
			m_appInterface->setIsoView1();
			break;
		case Mouse3DInput::V3DK_ISO2:
			m_appInterface->setIsoView2();
			break;
		case Mouse3DInput::V3DK_PLUS:
			//should be handled by the driver now!
			break;
		case Mouse3DInput::V3DK_MINUS:
			//should be handled by the driver now!
			break;
		case Mouse3DInput::V3DK_DOMINANT:
			//should be handled by the driver now!
			break;
		case Mouse3DInput::V3DK_CW:
		case Mouse3DInput::V3DK_CCW:
		{
			ccGLWindow* activeWin = m_appInterface->getActiveGLWindow();
			if (activeWin != nullptr)
			{
				CCVector3d axis(0,0,-1);
				CCVector3d trans(0,0,0);
				ccGLMatrixd mat;
				double angle = M_PI/2;
				if (key == Mouse3DInput::V3DK_CCW)
				{
					angle = -angle;
				}
				mat.initFromParameters(angle,axis,trans);
				activeWin->rotateBaseViewMat(mat);
				activeWin->redraw();
			}
		}
			break;
		case Mouse3DInput::V3DK_ESC:
		case Mouse3DInput::V3DK_ALT:
		case Mouse3DInput::V3DK_SHIFT:
		case Mouse3DInput::V3DK_CTRL:
		default:
			ccLog::Warning("[3D mouse] This button is not handled (yet)");
			//TODO
			break;
	}
}

void cc3DMouseManager::on3DMouseMove(std::vector<float>& vec)
{
	ccGLWindow* win = m_appInterface->getActiveGLWindow();
	if (win == nullptr)
		return;

	Mouse3DInput::Apply(vec, win);
}

void cc3DMouseManager::on3DMouseReleased()
{
	ccGLWindow* win = m_appInterface->getActiveGLWindow();
	if (win == nullptr)
		return;

	if (win->getPivotVisibility() == ccGLWindow::PIVOT_SHOW_ON_MOVE)
	{
		//we have to hide the pivot symbol!
		win->showPivotSymbol(false);
		win->redraw();
	}
}

void cc3DMouseManager::setupMenu()
{
	m_menu = new QMenu( "3D Mouse" );
	m_menu->setIcon( QIcon(":/CC/images/im3DxLogo.png") );

	m_actionEnable = new QAction( tr( "Enable" ), this );
	m_actionEnable->setCheckable( true );

	connect( m_actionEnable, &QAction::toggled, [this](bool state) {
		enableDevice(state, false);
	});

	m_menu->addAction( m_actionEnable );
}
