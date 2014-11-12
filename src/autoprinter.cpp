#include "autoprinter.h"
#include <QtGui>

//====================================================================================
#define SETTING_TEMPLATEFILE		"TemplateFile"
#define SETTING_SCANPATH			"ScanPath"
#define SETTING_OUTPUTPATH			"OutputPath"
#define SETTING_BACKUPPATH			"BackupPath"
#define SETTING_CONFIG_MASK_COORD	"MaskCoord"
#define SETTING_CONFIG_MASK_SIZE	"MaskSize"

#define TAB_INDEX_MAIN		0
#define TAB_INDEX_FRAME		1
#define TAB_INDEX_OUTPUT	2
#define TAB_INDEX_PRINTER	3

QStringList g_listPndinProcFiles;
QReadWriteLock g_pndinProcFilesLock;

QStringList g_listPndinPrintFiles;
QReadWriteLock g_pndinPrintFilesLock;

QWaitCondition g_conFileArrived;
QMutex g_mtxFileArrived;

QWaitCondition g_conPndinPrint;
QMutex g_mtxPndinPrint;

//====================================================================================
DirectoryMonitorThread::DirectoryMonitorThread( const QString &strDir )
	: m_strMonDir(strDir)
	, m_bRun(true)
{
}

void DirectoryMonitorThread::run()
{
	const int buf_size = 1024;  
	TCHAR buf[buf_size];  

	DWORD dwBufWrittenSize;  

	m_hMonDir = CreateFile(m_strMonDir.toStdString().c_str(), FILE_LIST_DIRECTORY, 
		FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING,   
		FILE_FLAG_BACKUP_SEMANTICS, NULL);   

	if (m_hMonDir == INVALID_HANDLE_VALUE)  
	{  
		DWORD dwErrorCode;  
		dwErrorCode = GetLastError();  
		return;
	}  

	while(m_bRun)  
	{  
		if(ReadDirectoryChangesW(m_hMonDir, &buf, buf_size, FALSE, 
			FILE_NOTIFY_CHANGE_FILE_NAME   
			, &dwBufWrittenSize, NULL, NULL))  
		{  
			FILE_NOTIFY_INFORMATION * pfiNotifyInfo = (FILE_NOTIFY_INFORMATION*)buf;  
			DWORD dwNextEntryOffset;  

			do  
			{  
				dwNextEntryOffset = pfiNotifyInfo->NextEntryOffset;  
				DWORD dwAction = pfiNotifyInfo->Action;   
				DWORD dwFileNameLength = pfiNotifyInfo->FileNameLength;  

				if (dwAction == FILE_ACTION_ADDED)
				{
					emit directoryChange();
				}

				if(dwNextEntryOffset != 0)  
				{  
					pfiNotifyInfo= (FILE_NOTIFY_INFORMATION*)((BYTE*)pfiNotifyInfo + dwNextEntryOffset);  
				}  

			}while (dwNextEntryOffset != 0);  
		}  
	}  
}

DirectoryMonitorThread::~DirectoryMonitorThread()
{
	::CloseHandle(m_hMonDir);
	m_hMonDir = NULL;
}

void DirectoryMonitorThread::Stop()
{
	m_bRun = false;
}

//====================================================================================
PhotoCombineThread::PhotoCombineThread(PhotoCombiner* pPhotoCombiner)
	: m_bRun(true)
	, m_pPhotoCombiner(pPhotoCombiner)
{

}

void PhotoCombineThread::run()
{
	while (m_bRun && m_pPhotoCombiner)
	{
		{
			QReadLocker locker(&g_pndinProcFilesLock);
			if (g_listPndinProcFiles.isEmpty())
			{
				locker.unlock();
				g_conFileArrived.wait(&g_mtxFileArrived);
				continue;
			}
		}
		
		{
			QWriteLocker locker(&g_pndinProcFilesLock);
			m_pPhotoCombiner->CombineImage(g_listPndinProcFiles.first());
			g_listPndinProcFiles.removeFirst();
		}
	}
}

//====================================================================================
PrinterThread::PrinterThread( PhotoPrinter* pPhotoPrinter )
	: m_bRun(true)
	, m_pPhotoPrinter(pPhotoPrinter)
{

}

void PrinterThread::run()
{
	while (m_bRun && m_pPhotoPrinter)
	{
		{
			QReadLocker locker(&g_pndinPrintFilesLock);
			if (g_listPndinPrintFiles.isEmpty())
			{
				locker.unlock();
				g_conPndinPrint.wait(&g_mtxPndinPrint);
				continue;
			}
		}

		{
			QWriteLocker lock(&g_pndinPrintFilesLock);
			m_pPhotoPrinter->PrintImage(g_listPndinPrintFiles.first());
			g_listPndinPrintFiles.removeFirst();
		}
	}
}


//====================================================================================
AutoScaledDisplayWidget::AutoScaledDisplayWidget( QWidget *parent /*= 0*/ )
	: QWidget(parent)
{

}

void AutoScaledDisplayWidget::SetDisplayImage( const QImage &image )
{
	m_image = image;
}

void AutoScaledDisplayWidget::paintEvent( QPaintEvent *event )
{
	QPainter painter(this);
	painter.drawImage(QPoint(0, 0), m_image.scaled(size(), Qt::KeepAspectRatio, 
		Qt::SmoothTransformation));
	painter.end();

	QWidget::paintEvent(event);
}

//====================================================================================
AutoPrinter::AutoPrinter(QWidget *parent, Qt::WFlags flags)
	: QMainWindow(parent, flags)
	, m_FgMaskPos(0, 0)
	, m_FgMaskSize(800, 600)
	, m_pMaskReviewWidget(NULL)
	, m_pOutputDispWidget(NULL)
	, m_pThreadMonitor(NULL)
	, m_pThreadCombine(NULL)
	, m_pThreadPrint(NULL)
	, m_pPrinter(NULL)
{
	QString strSetting = QApplication::applicationDirPath() + "/setting.ini";;
	m_pSettings = new QSettings(strSetting, QSettings::IniFormat, this);

	m_strTmptFilePath = m_pSettings->value(SETTING_TEMPLATEFILE).toString();
	m_strScanDir = m_pSettings->value(SETTING_SCANPATH).toString();
	m_strBackupDir = m_pSettings->value(SETTING_BACKUPPATH).toString();
	m_strOutputDir = m_pSettings->value(SETTING_OUTPUTPATH).toString();
	m_FgMaskPos = m_pSettings->value(SETTING_CONFIG_MASK_COORD).isNull() ? m_FgMaskPos : m_pSettings->value(SETTING_CONFIG_MASK_COORD).toPoint();
	m_FgMaskSize = m_pSettings->value(SETTING_CONFIG_MASK_SIZE).isNull() ? m_FgMaskSize : m_pSettings->value(SETTING_CONFIG_MASK_SIZE).toSize();

	ui.setupUi(this);
	
	// Print output tab's preview widget.
	m_pOutputDispWidget = new AutoScaledDisplayWidget();
	QVBoxLayout *pOutputLayout = new QVBoxLayout();
	pOutputLayout->setMargin(2);
	pOutputLayout->addWidget(m_pOutputDispWidget);
	ui.widgetOutputDisplay->setLayout(pOutputLayout);

	// Frame setting tab's preview widget.
	m_pMaskReviewWidget = new AutoScaledDisplayWidget();
	QVBoxLayout *pFrameReviewLayout = new QVBoxLayout();
	pFrameReviewLayout->setMargin(2);
	pFrameReviewLayout->addWidget(m_pMaskReviewWidget);
	ui.widgetFramePreview->setLayout(pFrameReviewLayout);

	// Frame setting tab's widget initial.
	ui.spBoxMaskWeight->setValue(m_FgMaskSize.width());
	ui.spBoxMaskHeight->setValue(m_FgMaskSize.height());
	ui.horzSliderImgHeight->setValue(m_FgMaskSize.height());
	ui.horzSliderImgWidth->setValue(m_FgMaskSize.width());
	ui.dSpBoxMaskCoordX->setValue(m_FgMaskPos.x());
	ui.dSpBoxMaskCoordY->setValue(m_FgMaskPos.y());
	ui.ImgHorzPosSlider->setValue(m_FgMaskPos.x());
	ui.ImgVertPosSlider->setValue(m_FgMaskPos.y());

	// Main setting, directory initial.
	ui.lnEdtTemplate->setText(m_strTmptFilePath);
	ui.lnEdtScanDir->setText(m_strScanDir);
	ui.lnEdtBackupDir->setText(m_strBackupDir);
	ui.lnEdtOutputDir->setText(m_strOutputDir);

	ui.btnStop->setEnabled(false);

	ui.spBoxCopyNum->setRange(1, 99);
	ui.spBoxCopyNum->setValue(1);

	ui.labCompletedCount->setText("0");
	ui.labOutputReview->setText("");

	// Kick start the print thread first.
	m_pThreadPrint = new PrinterThread(this);
	m_pThreadPrint->start();

	// Initial printers combobox.
	QPrinterInfo PrinterInfo;
	QList<QPrinterInfo> printerList = PrinterInfo.availablePrinters();
	for (int i = 0; i < printerList.size(); ++i)
	{
		PrinterInfo = printerList.at(i);
		QString printerName = PrinterInfo.printerName();
		ui.comboBoxPrinters->addItem(printerName);
		if (PrinterInfo.isDefault())
			ui.comboBoxPrinters->setCurrentIndex(i);
	}


	// Template File, Backup Dir, Output Dir, Scan Dir Path Select buttons.
	connect(ui.btnSelectScanPath, SIGNAL(pressed()), this, SLOT(OnSelectScanDir()));
	connect(ui.btnSelectBackupPath, SIGNAL(pressed()), this, SLOT(OnSelectBackupDir()));
	connect(ui.btnSelectOutputPath, SIGNAL(pressed()), this, SLOT(OnSelectOutputDir()));
	connect(ui.btnSelectTemplate, SIGNAL(pressed()), this, SLOT(OnSelectTemplatePath()));

	// Start, Stop, Save Setting function buttons.
	connect(ui.btnStart, SIGNAL(pressed()), this, SLOT(OnMonitorFolderStart()));
	connect(ui.btnStop, SIGNAL(pressed()), this, SLOT(OnMonitorFolderStop()));
	connect(ui.btnSaveSetting, SIGNAL(pressed()), this, SLOT(OnSaveSettings()));
	connect(ui.btnPrint, SIGNAL(pressed()), this, SLOT(OnPrintCopy()));

	// Mask width(height), Mask Coordinate.
	connect(ui.horzSliderImgWidth, SIGNAL(valueChanged(int)), this, SLOT(OnMaskWidthChange(int)));
	connect(ui.horzSliderImgHeight, SIGNAL(valueChanged(int)), this, SLOT(OnMaskHeightChange(int)));
	connect(ui.spBoxMaskHeight, SIGNAL(valueChanged(int)), this, SLOT(OnMaskHeightChangeSB(int)));
	connect(ui.spBoxMaskWeight, SIGNAL(valueChanged(int)), this, SLOT(OnMaskWidthChangeSB(int)));
	connect(ui.ImgHorzPosSlider, SIGNAL(valueChanged(int)), this, SLOT(OnMaskCoordChangeX(int)));
	connect(ui.ImgVertPosSlider, SIGNAL(valueChanged(int)), this, SLOT(OnMaskCoordChangeY(int)));
	connect(ui.dSpBoxMaskCoordX, SIGNAL(valueChanged(int)), this, SLOT(OnMaskCoordChangeXSB(int)));
	connect(ui.dSpBoxMaskCoordY, SIGNAL(valueChanged(int)), this, SLOT(OnMaskCoordChangeYSB(int)));

	// Tab change.
	connect(ui.tabWidget, SIGNAL(currentChanged(int)), this, SLOT(OnCurrentChanged(int)));

	// Output tab's widgets.
	connect(ui.lstViewOutput, SIGNAL(clicked(const QModelIndex &)), this, SLOT(OnOuputItemSelected(const QModelIndex &)));
	connect(ui.lnEdtFileName, SIGNAL(textChanged(const QString &)), this, SLOT(OnFindOutputFileName(const QString &)));
	connect(ui.btnRefreshOuputList, SIGNAL(pressed()), this, SLOT(OnUpdateOutputList()));
}

AutoPrinter::~AutoPrinter()
{
	if (m_pThreadMonitor)
	{
		m_pThreadMonitor->terminate();
		delete m_pThreadMonitor;
	}

	if (m_pThreadCombine)
	{
		m_pThreadCombine->terminate();
		delete m_pThreadCombine;
	}

	if (m_pThreadPrint)
	{
		m_pThreadPrint->terminate();
		delete m_pThreadPrint;
	}
	

	//m_pSettings->sync();
}

void AutoPrinter::paintEvent(QPaintEvent *event)
{
	QImage imgTemplate("K:\\test\\template.jpg");
	QImage imgInput("K:\\test\\input.png");
	QImage imgOutput(imgTemplate.size(), QImage::Format_ARGB32_Premultiplied);
	QImage imgMask(m_FgMaskSize, QImage::Format_ARGB32_Premultiplied);

	QBrush whiteBrush(Qt::white);
	QPainter maskPainter(&imgMask);
	maskPainter.setBackground(whiteBrush);

	QPainter outputPainter(&imgOutput);
	outputPainter.drawImage(QPoint(0, 0), imgTemplate);
	outputPainter.drawImage(m_FgMaskPos, imgMask);
	outputPainter.end();

	QPainter painter(this);
	painter.drawImage(QPoint(0, 0), imgOutput);
	painter.end();
	QMainWindow::paintEvent(event);	
}

void AutoPrinter::resizeEvent( QResizeEvent *event )
{
	//QImage image(m_strTmptFilePath);
	//QPalette pal(ui.widgetOutputDisplay->palette());
	//pal.setBrush(backgroundRole(), QBrush(image.scaled(ui.widgetOutputDisplay->size(), Qt::KeepAspectRatio, 
	//	Qt::FastTransformation)));
	//ui.widgetOutputDisplay->setPalette(pal);

	QMainWindow::resizeEvent(event);
}

void AutoPrinter::OnSelectScanDir()
{
	QFileDialog fileDialog(this);
	if (!m_strScanDir.isEmpty())
		fileDialog.setDirectory(m_strScanDir);
	m_strScanDir = fileDialog.getExistingDirectory();

	ui.lnEdtScanDir->setText(m_strScanDir);
	m_pSettings->setValue(SETTING_SCANPATH, m_strScanDir);
}

void AutoPrinter::OnSelectBackupDir()
{
	QFileDialog fileDialog(this);
	if (!m_strBackupDir.isEmpty())
		fileDialog.setDirectory(m_strBackupDir);
	m_strBackupDir = fileDialog.getExistingDirectory();

	ui.lnEdtBackupDir->setText(m_strBackupDir);
	m_pSettings->setValue(SETTING_BACKUPPATH, m_strBackupDir);
}

void AutoPrinter::OnSelectOutputDir()
{
	QFileDialog fileDialog(this);
	if (!m_strOutputDir.isEmpty())
		fileDialog.setDirectory(m_strOutputDir);
	m_strOutputDir = fileDialog.getExistingDirectory();

	ui.lnEdtOutputDir->setText(m_strOutputDir);
	m_pSettings->setValue(SETTING_OUTPUTPATH, m_strOutputDir);
}

void AutoPrinter::OnSelectTemplatePath()
{
	QFileDialog fileDialog(this);
	if (!m_strTmptFilePath.isEmpty())
		fileDialog.setDirectory(m_strTmptFilePath);
	m_strTmptFilePath = fileDialog.getOpenFileName();

	ui.lnEdtTemplate->setText(m_strTmptFilePath);
	m_pSettings->setValue(SETTING_TEMPLATEFILE, m_strTmptFilePath);
}

void AutoPrinter::OnMonitorFolderStart()
{
	if (m_strScanDir.isEmpty() || !QFile::exists(m_strScanDir))
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Please check your setting of scan path"));
		return;
	}

	if (m_strBackupDir.isEmpty() || !QFile::exists(m_strBackupDir))
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Please check your setting of backup path"));
		return;
	}
	InitialPrinter();

	m_imgTemplate = QImage(m_strTmptFilePath);

	if (m_pThreadMonitor && m_pThreadMonitor->isRunning())
	{
		m_pThreadMonitor->terminate();
		delete m_pThreadMonitor;
		m_pThreadMonitor = NULL;
	}

	if (m_pThreadMonitor == NULL)
	{
		m_pThreadMonitor = new DirectoryMonitorThread(m_strScanDir);
		connect(m_pThreadMonitor, SIGNAL(directoryChange()), this, SLOT(OnMonitorDirChange()));
	}

	if (m_pThreadCombine == NULL)
	{
		m_pThreadCombine = new PhotoCombineThread(this);
		m_pThreadCombine->start();
	}

	m_pThreadMonitor->start();

	ui.btnStart->setEnabled(false);
	ui.btnStop->setEnabled(true);
	ui.groupBoxFrameSetting->setEnabled(false);
	ui.groupBoxDirConfig->setEnabled(false);
}

void AutoPrinter::OnMonitorFolderStop()
{
	if (m_pThreadMonitor)
		m_pThreadMonitor->Stop();

	ui.btnStart->setEnabled(true);
	ui.btnStop->setEnabled(false);
	ui.groupBoxFrameSetting->setEnabled(true);
	ui.groupBoxDirConfig->setEnabled(true);
}

void AutoPrinter::OnSaveSettings()
{
	m_pSettings->setValue(SETTING_CONFIG_MASK_COORD, m_FgMaskPos);
	m_pSettings->setValue(SETTING_CONFIG_MASK_SIZE, m_FgMaskSize);
}

void AutoPrinter::InitTemplatePreviewTab()
{
	if (m_strTmptFilePath.isEmpty() || !QFile::exists(m_strTmptFilePath) || !ui.groupBoxDirConfig->isEnabled())
	{
		ui.groupBoxFrameSetting->setEnabled(false);
	}
	else
	{
		ui.groupBoxFrameSetting->setEnabled(true);
	}

	// Update slider max value once update the template image.
	m_imgTemplate = QImage(m_strTmptFilePath);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	UpdateMaskConfigRange();
}

void AutoPrinter::UpdateMaskConfigRange()
{
	QSize size = m_imgTemplate.size();
	ui.ImgHorzPosSlider->setRange(0, size.width() - m_FgMaskSize.width());
	ui.ImgVertPosSlider->setRange(0, size.height() - m_FgMaskSize.height());
	ui.dSpBoxMaskCoordX->setRange(0, size.width() - m_FgMaskSize.width());
	ui.dSpBoxMaskCoordY->setRange(0, size.height() - m_FgMaskSize.height());
	ui.spBoxMaskHeight->setRange(0, size.width());
	ui.spBoxMaskWeight->setRange(0, size.height());
	ui.horzSliderImgHeight->setRange(0, size.height());
	ui.horzSliderImgWidth->setRange(0, size.width());

	ui.ImgHorzPosSlider->update();
	ui.ImgVertPosSlider->update();
	ui.dSpBoxMaskCoordX->update();
	ui.dSpBoxMaskCoordY->update();
	ui.spBoxMaskHeight->update();
	ui.spBoxMaskWeight->update();
	ui.horzSliderImgHeight->update();
	ui.horzSliderImgWidth->update();
}

QImage AutoPrinter::GetFramePreviewImage()
{
	QImage imgRet(m_imgTemplate.size(), QImage::Format_ARGB32_Premultiplied);

	QPainter painterPre(&imgRet);
	QBrush whiteBrush(Qt::white);
	painterPre.drawImage(QPoint(0, 0), m_imgTemplate);
	QRect rect(m_FgMaskPos, m_FgMaskSize);
	painterPre.fillRect(rect, whiteBrush);
	painterPre.end();

	return imgRet;
}

void AutoPrinter::OnMaskHeightChange( int val )
{
	m_FgMaskSize.setHeight(val);
	ui.spBoxMaskHeight->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	UpdateMaskConfigRange();
}

void AutoPrinter::OnMaskWidthChange( int val )
{
	m_FgMaskSize.setWidth(val);
	ui.spBoxMaskWeight->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	UpdateMaskConfigRange();
}

void AutoPrinter::OnMaskHeightChangeSB( int val )
{
	m_FgMaskSize.setHeight(val);
	ui.horzSliderImgHeight->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	UpdateMaskConfigRange();
}

void AutoPrinter::OnMaskWidthChangeSB( int val )
{
	m_FgMaskSize.setWidth(val);
	ui.horzSliderImgWidth->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	UpdateMaskConfigRange();
}

void AutoPrinter::OnMaskCoordChangeX(int val)
{
	m_FgMaskPos.setX(val);
	ui.dSpBoxMaskCoordX->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	UpdateMaskConfigRange();
}

void AutoPrinter::OnMaskCoordChangeY(int val)
{
	m_FgMaskPos.setY(val);
	ui.dSpBoxMaskCoordY->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	UpdateMaskConfigRange();
}

void AutoPrinter::OnMaskCoordChangeXSB(int val)
{
	m_FgMaskPos.setX(val);
	ui.ImgHorzPosSlider->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	UpdateMaskConfigRange();
}

void AutoPrinter::OnMaskCoordChangeYSB(int val)
{
	m_FgMaskPos.setY(val);
	ui.ImgVertPosSlider->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	UpdateMaskConfigRange();
}

void AutoPrinter::OnMonitorDirChange()
{
	QDir scanDir(m_strScanDir);
	if (scanDir.count() == 0)
		return;

	QFileInfoList fileInfoList = scanDir.entryInfoList(
		QDir::Files|QDir::NoSymLinks|QDir::NoDotAndDotDot
		|QDir::NoDot|QDir::NoDotDot);

	QFileInfoList::iterator it;
	for (it = fileInfoList.begin(); it != fileInfoList.end(); ++it)
	{
		QFile file(it->absoluteFilePath());
		QString newPath = m_strBackupDir + "\\" + it->fileName();
		QFile::rename(it->absoluteFilePath(), newPath);

		QWriteLocker lock(&g_pndinProcFilesLock);
		g_listPndinProcFiles.push_back(it->fileName());
		g_listPndinProcFiles.removeDuplicates();

		g_conFileArrived.wakeAll();
	}

}

void AutoPrinter::CombineImage( const QString &strInputImage )
{
	if (!QFile::exists(m_strTmptFilePath))
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Please check your setting of template image path"));
		return;
	}

	QString strImgBackupPath = m_strBackupDir + "\\" + strInputImage;
	QString strImgOutputPath = m_strOutputDir + "\\" + strInputImage;

	if (!QFile::exists(strImgBackupPath))
		return;

	QImage imgInput(strImgBackupPath);
	QImage imgOutput(m_imgTemplate.size(), QImage::Format_ARGB32_Premultiplied);

	QPainter outputPainter(&imgOutput);
	outputPainter.drawImage(QPoint(0, 0), m_imgTemplate);
	outputPainter.drawImage(m_FgMaskPos, imgInput.scaled(m_FgMaskSize, Qt::KeepAspectRatio, 
		Qt::SmoothTransformation));
	outputPainter.end();

	imgOutput.save(strImgOutputPath);

	AddPendingPrintImage(strImgOutputPath);
}

void AutoPrinter::PrintImage( const QString &strImagePath )
{
	QImage image(strImagePath);
	QPainter painter;
	painter.begin(m_pPrinter);
	painter.drawImage(QPoint(0, 0), image);
	painter.end();
}

void AutoPrinter::InitPrinterSelectTab()
{
	// TODO:
}

void AutoPrinter::OnCurrentChanged(int nIndex)
{
	switch(nIndex)
	{
	case TAB_INDEX_PRINTER:
		{
			InitPrinterSelectTab();
		}
		break;
	case TAB_INDEX_OUTPUT:
		{
			InitCombineOutputTab();
		}
		break;
	case TAB_INDEX_FRAME:
		{
			InitTemplatePreviewTab();
		}
		break;
	default:
		break;
	}
}

void AutoPrinter::AddPendingPrintImage(const QString &strImagePath, int nTimes, bool bPreempt)
{
	QWriteLocker lock(&g_pndinPrintFilesLock);
	int i = 0;
	do 
	{
		if (bPreempt)
			g_listPndinPrintFiles.push_front(strImagePath);
		else
			g_listPndinPrintFiles.push_back(strImagePath);

	} while (++i < nTimes);

	g_conPndinPrint.wakeAll();
}

void AutoPrinter::OnPrintCopy()
{
	if (ui.labOutputReview->text().isEmpty())
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Please select a output image first!"));
		return;
	}

	QString filePath = m_strOutputDir + "\\" + ui.labOutputReview->text();
	if (!QFile::exists(filePath))
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Select image not exist!"));
		return;
	}
	
	InitialPrinter();
	AddPendingPrintImage(filePath, ui.spBoxCopyNum->value(), true);	
}

void AutoPrinter::OnUpdateOutputList()
{
	if (!QFile::exists(m_strOutputDir))
		return;

	QDir outputDir(m_strOutputDir);
	if (outputDir.count() == 0)
		return;

	QFileInfoList fileInfoList = outputDir.entryInfoList(
		QDir::Files|QDir::NoSymLinks|QDir::NoDotAndDotDot
		|QDir::NoDot|QDir::NoDotDot);

	QFileInfoList::iterator it;
	QStandardItemModel* model = new QStandardItemModel(ui.lstViewOutput);
	int i = 0;
	for (it = fileInfoList.begin(); it != fileInfoList.end(); ++it)
	{
		QStandardItem* item = new QStandardItem(it->fileName());
		item->setEditable(false);
		model->setItem(i, item);
		++i;
	}
	ui.lstViewOutput->setModel(model);
}

void AutoPrinter::InitCombineOutputTab()
{
	OnUpdateOutputList();
}

void AutoPrinter::OnOuputItemSelected( const QModelIndex &index )
{
	QString strSelectedImg = index.model()->data(index, Qt::DisplayRole).toString();
	ui.labOutputReview->setText(strSelectedImg);

	QString strImgPath = m_strOutputDir + "\\" + strSelectedImg;
	if (QFile::exists(strImgPath))
	{
		QImage image(strImgPath);
		m_pOutputDispWidget->SetDisplayImage(image);
	}
	
}

void AutoPrinter::OnFindOutputFileName( const QString& string )
{
	ui.lstViewOutput->keyboardSearch(string);
}

void AutoPrinter::InitialPrinter()
{
	if (m_pPrinter == NULL)
	{
		m_pPrinter = new QPrinter(QPrinter::HighResolution);
		QPrintDialog* dlg = new QPrintDialog(m_pPrinter); 
		if(dlg->exec() == QDialog::Accepted) { 

		} 
		delete dlg; 
	}
}
