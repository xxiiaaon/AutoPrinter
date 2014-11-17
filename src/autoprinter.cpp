#include "autoprinter.h"
#include <QtGui>

//====================================================================================
#define SETTING_TOTAL_OUTPUT				"TotalOutput"
#define SETTING_TEMPLATEFILE				"TemplateFile"
#define SETTING_SCANPATH					"ScanPath"
#define SETTING_OUTPUTPATH					"OutputPath"
#define SETTING_BACKUPPATH					"BackupPath"
#define SETTING_CONFIG_MASK_COORD			"MaskCoord"
#define SETTING_CONFIG_MASK_SIZE			"MaskSize"
#define SETTING_CONFIG_PAPER_WIDTH			"PaperWidth"
#define SETTING_CONFIG_PAPER_HEIGHT			"PaperHeight"
#define SETTING_CONFIG_WATERMARK_FONT		"WaterMarkFont"
#define SETTING_CONFIG_WATERMARK_SIZE		"WaterMarkSize"
#define SETTING_CONFIG_WATERMARK_COLOR		"WaterMarkColor"
#define SETTING_CONFIG_WATERMARK_COORD		"WaterMarkCoord"
#define SETTING_CONFIG_WATERMARK_ENABLE		"WaterMarkEnable"
#define SETTING_CONFIG_OUTPUT_SUFFIX		"OutputSuffix"
#define SETTING_CONFIG_OUTPUT_NAME_TYPE		"OutputNameType"

#define TAB_INDEX_MAIN		0
#define TAB_INDEX_FRAME		1
#define TAB_INDEX_OUTPUT	2
#define TAB_INDEX_PRINTER	3

#define JPEG	"*.jpg"
#define PNG		"*.png"
#define BMP		"*.bmp"

const QStringList g_strLstSupFormat = (QStringList() << JPEG << PNG << BMP);

// Thread concurrent relatives.
// Pending process files list control.
QStringList g_listPndinProcFiles;
QReadWriteLock g_pndinProcFilesLock;

// Pending print files list control.
QStringList g_listPndinPrintFiles;
QReadWriteLock g_pndinPrintFilesLock;

// New file arrived.
QWaitCondition g_conFileArrived;
QMutex g_mtxFileArrived;

// Print print file list.
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

				// If action is add, them notify.
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
PhotoCombineThread::PhotoCombineThread(PhotoCombinerInterface* pPhotoCombiner)
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
		
		QString strInputImage;
		{	// Retieve the first image name in pending queue.
			QReadLocker locker(&g_pndinProcFilesLock);
			strInputImage = g_listPndinProcFiles.first();
		}
		m_pPhotoCombiner->CombineImage(strInputImage);

		{	// Remove the first image nage from queue after processing.
			QWriteLocker locker(&g_pndinProcFilesLock);
			g_listPndinProcFiles.removeFirst();
		}
	}
}

//====================================================================================
PrinterThread::PrinterThread( PhotoPrinterInterface* pPhotoPrinter )
	: m_bRun(true)
	, m_pPhotoPrinter(pPhotoPrinter)
{

}

void PrinterThread::run()
{
	while (m_bRun && m_pPhotoPrinter)
	{
		{	// Wait if queue empty.
			QReadLocker locker(&g_pndinPrintFilesLock);
			if (g_listPndinPrintFiles.isEmpty())
			{
				locker.unlock();
				g_conPndinPrint.wait(&g_mtxPndinPrint);
				continue;
			}
		}
		QString strImagePath;
		{	// Retieve the first image name in print queue.
			QReadLocker locker(&g_pndinPrintFilesLock);
			strImagePath = g_listPndinPrintFiles.first();
		}

		// Can't print in none gui thread?
		// m_pPhotoPrinter->PrintImage(strImagePath);
		emit PrintImage(strImagePath); // Print in main thread.

		{	// Remove the first image nage from queue after printing.
			QWriteLocker locker(&g_pndinPrintFilesLock);
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
	, m_pPrintReviewWidget(NULL)
	, m_pThreadMonitor(NULL)
	, m_pThreadCombine(NULL)
	, m_pThreadPrint(NULL)
	, m_pPrinter(NULL)
	, m_nCompletedCount(0)
	, m_bPaperWidth(0.0)
	, m_bPaperHeight(0.0)
	, m_waterMarkFont(QFont("Microsoft JhengHei", 20, 10))
	, m_waterMarkColor(QColor(Qt::black))
	, m_nTotolOutput(0)
{
	ui.setupUi(this);

	QString strSetting = QApplication::applicationDirPath() + "/setting.ini";;
	m_pSettings = new QSettings(strSetting, QSettings::IniFormat, this);

	// Retrive for setting.ini.
	m_strTmptFilePath = m_pSettings->value(SETTING_TEMPLATEFILE).toString();
	m_strScanDir = m_pSettings->value(SETTING_SCANPATH).toString();
	m_strBackupDir = m_pSettings->value(SETTING_BACKUPPATH).toString();
	m_strOutputDir = m_pSettings->value(SETTING_OUTPUTPATH).toString();
	m_FgMaskPos = m_pSettings->value(SETTING_CONFIG_MASK_COORD).isNull() ? m_FgMaskPos : m_pSettings->value(SETTING_CONFIG_MASK_COORD).toPoint();
	m_FgMaskSize = m_pSettings->value(SETTING_CONFIG_MASK_SIZE).isNull() ? m_FgMaskSize : m_pSettings->value(SETTING_CONFIG_MASK_SIZE).toSize();
	m_bPaperWidth = m_pSettings->value(SETTING_CONFIG_PAPER_WIDTH).isNull() ? m_bPaperWidth : m_pSettings->value(SETTING_CONFIG_PAPER_WIDTH).toDouble();
	m_bPaperHeight = m_pSettings->value(SETTING_CONFIG_PAPER_HEIGHT).isNull() ? m_bPaperHeight : m_pSettings->value(SETTING_CONFIG_PAPER_HEIGHT).toDouble();
	if (!m_pSettings->value(SETTING_CONFIG_WATERMARK_FONT).isNull())
		m_waterMarkFont.setFamily(m_pSettings->value(SETTING_CONFIG_WATERMARK_FONT).toString());
	if (!m_pSettings->value(SETTING_CONFIG_WATERMARK_SIZE).isNull())
		m_waterMarkFont.setPointSize(m_pSettings->value(SETTING_CONFIG_WATERMARK_SIZE).toInt());
	if (!m_pSettings->value(SETTING_CONFIG_WATERMARK_COLOR).isNull())
		m_waterMarkColor.setRgb(m_pSettings->value(SETTING_CONFIG_WATERMARK_COLOR).toInt());
	if (!m_pSettings->value(SETTING_CONFIG_WATERMARK_COORD).isNull())
		m_waterMarkPos = m_pSettings->value(SETTING_CONFIG_WATERMARK_COORD).toPoint();
	if (!m_pSettings->value(SETTING_CONFIG_WATERMARK_ENABLE).isNull())
	{
		bool bWaterMarkEnable = m_pSettings->value(SETTING_CONFIG_WATERMARK_ENABLE).toBool();
		ui.gpBoxWaterMarkSetting->setEnabled(bWaterMarkEnable);
		ui.chkboxWaterMark->setChecked(bWaterMarkEnable);
	}
	if (!m_pSettings->value(SETTING_CONFIG_OUTPUT_SUFFIX).isNull())
		ui.lnEditSuffix->setText(m_pSettings->value(SETTING_CONFIG_OUTPUT_SUFFIX).toString());
	if (!m_pSettings->value(SETTING_TOTAL_OUTPUT).isNull())
		m_nTotolOutput = m_pSettings->value(SETTING_TOTAL_OUTPUT).toUInt();
			

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

	// Printing review widget.
	m_pPrintReviewWidget = new AutoScaledDisplayWidget();
	QVBoxLayout *pPrintingReviewLayout = new QVBoxLayout();
	pPrintingReviewLayout->setMargin(2);
	pPrintingReviewLayout->addWidget(m_pPrintReviewWidget);
	ui.widgetPrintingReview->setLayout(pPrintingReviewLayout);

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

	// Printing Review Tab's widget initial.
	ui.btnPrintCancel->setEnabled(false);
	ui.btnPrintCancel->setVisible(false);
	ui.dspBoxPaperHeight->setValue(m_bPaperHeight);
	ui.dspBoxPaperWidth->setValue(m_bPaperWidth);

	ui.btnStop->setEnabled(false);

	ui.spBoxCopyNum->setRange(1, 99);
	ui.spBoxCopyNum->setValue(1);

	ui.labCompletedCount->setText(QString::number(m_nCompletedCount));
	ui.labCompletedCount->setFont(QFont("Helvetica", 35, 10));
	ui.label_5->setFont(QFont("Microsoft JhengHei", 20, 10));
	ui.labOutputReview->setText("");

	// Kick start the print thread first.
	m_pThreadPrint = new PrinterThread(this);
	connect(m_pThreadPrint, SIGNAL(PrintImage(const QString &)), this, SLOT(OnPrintImage(const QString &)));
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

	ui.lstViewPrinters->installEventFilter(this);

	// Tab change.
	connect(ui.tabWidget, SIGNAL(currentChanged(int)), this, SLOT(OnCurrentChanged(int)));

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

	// Frame Setting tab's widgets.
	connect(ui.btnChangeWaterMarkFont, SIGNAL(pressed()), this, SLOT(OnChangeWaterMarkFont()));
	connect(ui.btnChangeWaterMakeColor, SIGNAL(pressed()), this, SLOT(OnChangeWaterMarkFontColor()));
	connect(ui.horzSliderWaterMarkCoordX, SIGNAL(valueChanged(int)), this, SLOT(OnWaterMarkCoordChangeX(int)));
	connect(ui.horzSliderWaterMarkCoordY, SIGNAL(valueChanged(int)), this, SLOT(OnWaterMarkCoordChangeY(int)));
	connect(ui.spBoxWaterMarkCoordX, SIGNAL(valueChanged(int)), this, SLOT(OnWaterMarkCoordChangeXSB(int)));
	connect(ui.spBoxWaterMarkCoordY, SIGNAL(valueChanged(int)), this, SLOT(OnWaterMarkCoordChangeYSB(int)));
	connect(ui.chkboxWaterMark, SIGNAL(stateChanged(int)), this, SLOT(OnWaterMarkEnable(int)));

	// Output tab's widgets.
	connect(ui.lstViewOutput, SIGNAL(clicked(const QModelIndex &)), this, SLOT(OnOuputItemSelected(const QModelIndex &)));
	connect(ui.lstViewOutput, SIGNAL(activated(const QModelIndex &)), this, SLOT(OnOuputItemSelected(const QModelIndex &)));
	connect(ui.lnEdtFileName, SIGNAL(textChanged(const QString &)), this, SLOT(OnFindOutputFileName(const QString &)));
	connect(ui.btnRefreshOuputList, SIGNAL(pressed()), this, SLOT(OnUpdateOutputList()));

	// Printer tab's widgets.
	connect(ui.lstViewPrinters, SIGNAL(clicked(const QModelIndex &)), this, SLOT(OnPrintingItemSelected(const QModelIndex &)));
	connect(ui.lstViewPrinters, SIGNAL(activated(const QModelIndex &)), this, SLOT(OnPrintingItemSelected(const QModelIndex &)));
	connect(ui.btnPrintCancel, SIGNAL(pressed()), this, SLOT(OnCancelPrintItem()));
	connect(ui.dspBoxPaperWidth, SIGNAL(valueChanged(double)), this, SLOT(OnPaperWidthChange(double)));
	connect(ui.dspBoxPaperHeight, SIGNAL(valueChanged(double)), this, SLOT(OnPaperHeightChange(double)));
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

// TODO: Strongly recommand to refactor this function afterwards.
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

	// Delete the exist monitoring thread first.
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

	// Create a combiner thread.
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
	// Configure.
	m_pSettings->setValue(SETTING_CONFIG_OUTPUT_SUFFIX, ui.lnEditSuffix->text());

	// Mask Coord and Size.
	m_pSettings->setValue(SETTING_CONFIG_MASK_COORD, m_FgMaskPos);
	m_pSettings->setValue(SETTING_CONFIG_MASK_SIZE, m_FgMaskSize);

	// Water Mark.
	m_pSettings->setValue(SETTING_CONFIG_WATERMARK_FONT, m_waterMarkFont.family());
	m_pSettings->setValue(SETTING_CONFIG_WATERMARK_SIZE, m_waterMarkFont.pointSizeF());
	m_pSettings->setValue(SETTING_CONFIG_WATERMARK_COLOR, m_waterMarkColor.rgb());
	m_pSettings->setValue(SETTING_CONFIG_WATERMARK_ENABLE, ui.chkboxWaterMark->isChecked());
	m_pSettings->setValue(SETTING_CONFIG_WATERMARK_COORD, m_waterMarkPos);
}

void AutoPrinter::InitPreviewTab()
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

	// Update water mark coordinate range.
	QSize size = m_imgTemplate.size();
	ui.spBoxWaterMarkCoordX->setRange(0, size.width());
	ui.spBoxWaterMarkCoordY->setRange(0, size.height());
	ui.horzSliderWaterMarkCoordX->setRange(0, size.width());
	ui.horzSliderWaterMarkCoordY->setRange(0, size.height());
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

// Combine the mask preview image for mask frame setting tab.
QImage AutoPrinter::GetFramePreviewImage()
{
	QImage imgRet(m_imgTemplate.size(), QImage::Format_ARGB32_Premultiplied);

	// Draw a blank mask on the template image for preview.
	QPainter painterPre(&imgRet);
	QBrush whiteBrush(QColor(255, 0, 0, 80));
	QRect rect(m_FgMaskPos, m_FgMaskSize);
	painterPre.drawImage(QPoint(0, 0), m_imgTemplate);
	painterPre.fillRect(rect, whiteBrush);
	if (ui.chkboxWaterMark->isChecked())
	{
		painterPre.setFont(m_waterMarkFont);
		painterPre.setPen(QPen(m_waterMarkColor));
		painterPre.drawText(m_waterMarkPos, "1234567890");
	}
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

void AutoPrinter::OnWaterMarkCoordChangeX( int val )
{
	m_waterMarkPos.setX(val);
	ui.spBoxWaterMarkCoordX->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	ui.widgetFramePreview->update();
}

void AutoPrinter::OnWaterMarkCoordChangeY( int val )
{
	m_waterMarkPos.setY(val);
	ui.spBoxWaterMarkCoordY->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	ui.widgetFramePreview->update();
}

void AutoPrinter::OnWaterMarkCoordChangeXSB( int val )
{
	m_waterMarkPos.setX(val);
	ui.horzSliderWaterMarkCoordX->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	ui.widgetFramePreview->update();
}

void AutoPrinter::OnWaterMarkCoordChangeYSB( int val )
{
	m_waterMarkPos.setY(val);
	ui.horzSliderWaterMarkCoordY->setValue(val);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	ui.widgetFramePreview->update();
}

// Iterate all files in Scan dir and move them to backup dir.
void AutoPrinter::OnMonitorDirChange()
{
	QDir scanDir(m_strScanDir);
	if (scanDir.count() == 0)
	{
		return;
	}

	QFileInfoList fileInfoList = scanDir.entryInfoList(
		g_strLstSupFormat,
		QDir::Files|QDir::NoSymLinks|QDir::NoDotAndDotDot
		|QDir::NoDot|QDir::NoDotDot);

	QFileInfoList::iterator it;
	for (it = fileInfoList.begin(); it != fileInfoList.end(); ++it)
	{
		QFile file(it->absoluteFilePath());
		QString newPath = m_strBackupDir + "\\" + it->fileName();
		//bool bSuccess = QFile::rename(it->absoluteFilePath(), newPath);
		bool bSuccess = ::MoveFile(it->absoluteFilePath().toStdString().c_str(), newPath.toStdString().c_str());

		if (!bSuccess)
		{
			QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("File \"%1\" is existed!").arg(it->absoluteFilePath()));
			continue;
		}

		Sleep(1000);

		// Put into pending process queue wait for combine thread to deal with.
		QWriteLocker lock(&g_pndinProcFilesLock);
		g_listPndinProcFiles.push_back(it->fileName());
		g_listPndinProcFiles.removeDuplicates();

		// Wake up combine thread once file ready.
		g_conFileArrived.wakeAll();
	}

}

// This combine function for output.
void AutoPrinter::CombineImage( const QString &strInputImage )
{
	if (!QFile::exists(m_strTmptFilePath))
	{
		return;
	}

	QString strImgBackupPath = m_strBackupDir + "\\" + strInputImage;

	// TODO: Configure output image name style.
	// Directly adding suffix.
	int nIndex = strInputImage.indexOf(".");
	QString strImgFormat = strInputImage.right(nIndex - 1);
	QString strOutputImage;
	QString strWaterMark;

	if (ui.chkBoxNormalSuffix->isChecked())
	{
		QString strOutputImgName = strInputImage.left(nIndex).append(ui.lnEditSuffix->text());
		strWaterMark = strOutputImgName;
		strOutputImage = strOutputImgName + strImgFormat;
	}
	else
	{
		QString strGenNumber = QString("%1").arg(m_nTotolOutput, 10, 10, QChar('0'));
		strWaterMark = strGenNumber;
		strOutputImage = strGenNumber + strImgFormat;

		m_nTotolOutput++;
		m_pSettings->setValue(SETTING_TOTAL_OUTPUT, m_nTotolOutput);
	}

	QString strImgOutputPath = m_strOutputDir + "\\" + strOutputImage;

	// Corresponding backup image not exist.
	if (!QFile::exists(strImgBackupPath))
		return;

	QImage imgInput(strImgBackupPath);
	QImage imgOutput(m_imgTemplate.size(), QImage::Format_ARGB32_Premultiplied);

	// Draw the input image one the template, the scaled way may need to change.
	QPainter outputPainter(&imgOutput);
	outputPainter.drawImage(m_FgMaskPos, imgInput.scaled(m_FgMaskSize, Qt::KeepAspectRatioByExpanding, // TODO:
		Qt::SmoothTransformation));
	outputPainter.drawImage(QPoint(0, 0), m_imgTemplate);
	if (ui.chkboxWaterMark->isChecked())
	{
		outputPainter.setFont(m_waterMarkFont);
		outputPainter.setPen(QPen(m_waterMarkColor));
		outputPainter.drawText(m_waterMarkPos, strWaterMark);
	}

	// TODO: If need, add the watermark.

	outputPainter.end();

	if (!imgOutput.save(strImgOutputPath))
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("File \"%1\" is existed!").arg(strImgOutputPath));
		return;
	}

	AddPendingPrintImage(strImgOutputPath);
}

void AutoPrinter::OnPrintImage( const QString &strImagePath )
{
	OnUpdatePrintingList();

	QImage image(strImagePath);

	// TODO: Add setting to configure printer parameters.
	m_pPrinter->setFullPage(true);
	if (ui.chkBoxLandscape->isChecked())
	{
		m_pPrinter->setOrientation(QPrinter::Landscape);
		QSizeF paperSize(m_bPaperWidth, m_bPaperHeight);
		m_pPrinter->setPaperSize(paperSize, QPrinter::Millimeter);
	}
	else
	{
		m_pPrinter->setOrientation(QPrinter::Portrait);
		QSizeF paperSize(m_bPaperWidth, m_bPaperHeight);
		m_pPrinter->setPaperSize(paperSize, QPrinter::Millimeter);
	}

	// TODO: Printer's painter way 1st:
	QPainter painter(m_pPrinter);
	QRect rect = painter.viewport();
	QSize size = image.size();
	size.scale(rect.size(), Qt::KeepAspectRatioByExpanding);
	painter.setViewport(rect.x(), rect.y(),
		size.width(), size.height());
	painter.setWindow(image.rect());
	painter.drawImage(0, 0, image);

	// TODO: Printer's painter way 2nd:
	//QPainter painter(m_pPrinter);
	//QRect rect = painter.viewport();
	//painter.begin(m_pPrinter);
	//painter.drawImage(QPoint(0, 0), image.scaled(rect.width(), rect.height(), Qt::KeepAspectRatio));
	//painter.end();


	m_pPrinter->newPage();
	m_nCompletedCount++;
	ui.labCompletedCount->setText(QString::number(m_nCompletedCount));


}

void AutoPrinter::InitPrinterTab()
{
	// Initial.
	ui.labPrintingObj->setText("");
	ui.btnPrintCancel->setEnabled(false);
	ui.btnPrintCancel->setVisible(false);
	m_pPrintReviewWidget->setVisible(false);

	// Update Printing list.
	OnUpdatePrintingList();

	// For setting up preview, preview button if is printing.
	QStandardItemModel* model = static_cast<QStandardItemModel*>(ui.lstViewPrinters->model());
	// Get the first item which should be the printing item.

	if (model)
	{
		QStandardItem* item = model->item(0, 0);

		if (item)
		{
			// Get printing image name.
			QString strImage = item->text();
			ui.labPrintingObj->setText(AutoPrinter::tr("Printing... ") + strImage);

			// Set image to preview widget for displaying.
			QImage image(strImage);
			m_pPrintReviewWidget->SetDisplayImage(image);
			m_pPrintReviewWidget->setVisible(true);

			// Visable cancel button, but disable.
			ui.btnPrintCancel->setVisible(true);
			ui.btnPrintCancel->setEnabled(false);
		}
	}
}

void AutoPrinter::OnUpdatePrintingList()
{
	QStandardItemModel* model = new QStandardItemModel(ui.lstViewOutput);
	{
		QReadLocker locker(&g_pndinPrintFilesLock);
		QStringList::iterator it;
		int i = 0;
		for (it = g_listPndinPrintFiles.begin(); it != g_listPndinPrintFiles.end(); ++it)
		{
			QStandardItem* item = new QStandardItem(*it);
			item->setEditable(false);
			model->setItem(i, item);

			if (i == 0)
			{
				QColor txtColor = Qt::darkBlue;
				QColor bgColor = Qt::green;
				item->setData(txtColor, Qt::TextColorRole);
				item->setData(bgColor, Qt::BackgroundRole);
			}
			++i;
		}
	}
	ui.lstViewPrinters->setModel(model);
}

void AutoPrinter::OnCurrentChanged(int nIndex)
{
	switch(nIndex)
	{
	case TAB_INDEX_PRINTER:
		{
			InitPrinterTab();
		}
		break;
	case TAB_INDEX_OUTPUT:
		{
			InitOutputTab();
		}
		break;
	case TAB_INDEX_FRAME:
		{
			InitPreviewTab();
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

// Iterate all image file in the ourput direcotry.
void AutoPrinter::OnUpdateOutputList()
{
	if (!QFile::exists(m_strOutputDir))
		return;

	QDir outputDir(m_strOutputDir);
	if (outputDir.count() == 0)
		return;

	QFileInfoList fileInfoList = outputDir.entryInfoList(
		g_strLstSupFormat, 
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

void AutoPrinter::InitOutputTab()
{
	// Initial.
	// TODO:

	// Update output list data.
	OnUpdateOutputList();

}

void AutoPrinter::OnOuputItemSelected( const QModelIndex &index )
{
	// Get Select image name.
	QString strSelectedImg = index.model()->data(index, Qt::DisplayRole).toString();

	// Set search box text.
	ui.lnEdtFileName->setText(strSelectedImg);

	// Set preview image label.
	ui.labOutputReview->setText(strSelectedImg);

	// Set preview image.
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
		m_pPrinter = new QPrinter();
		QPrintDialog* dlg = new QPrintDialog(m_pPrinter); 
		if(dlg->exec() == QDialog::Accepted) { 

		} 
		delete dlg; 

	}
}

bool AutoPrinter::eventFilter( QObject *obj, QEvent *event )
{
	if (obj == ui.lstViewPrinters) {
		if (event->type() == QEvent::KeyPress) {
			QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
			if (keyEvent->key() == Qt::Key_Delete)
			{
				OnCancelPrintItem();
			}
			return true;
		} else {
			return false;
		}
	} else {
		// Pass the event on to the parent class.
		return QMainWindow::eventFilter(obj, event);
	}
}

void AutoPrinter::OnPrintingItemSelected( const QModelIndex &index )
{
	// Get Select image name.
	QString strSelectedImg = index.model()->data(index, Qt::DisplayRole).toString();

	bool bIndexPrinting = false;
	if (0 == index.row())
		bIndexPrinting = true;

	QStandardItemModel* model = static_cast<QStandardItemModel*>(ui.lstViewPrinters->model());
	if (model)
	{
		QStandardItem* item = model->item(0, 0);
	}

	// Set preview image.
	if (QFile::exists(strSelectedImg))
	{
		QImage image(strSelectedImg);
		m_pPrintReviewWidget->SetDisplayImage(image);
		m_pPrintReviewWidget->setVisible(true);

		if (bIndexPrinting)
		{
			ui.btnPrintCancel->setEnabled(false);
			ui.btnPrintCancel->setVisible(true);
			ui.labPrintingObj->setText(AutoPrinter::tr("Printing... ") + strSelectedImg);
		}
		else
		{
			ui.btnPrintCancel->setEnabled(true);
			ui.btnPrintCancel->setVisible(true);
			ui.labPrintingObj->setText(strSelectedImg);
		}
	}
}

void AutoPrinter::OnCancelPrintItem()
{
	QModelIndex currentIndex = ui.lstViewPrinters->currentIndex();
	if (!currentIndex.isValid() || currentIndex.row() == 0)
		return;

	int index = currentIndex.row();
	{
		QWriteLocker locker(&g_pndinPrintFilesLock);
		g_listPndinPrintFiles.removeAt(index);
	}

	OnUpdatePrintingList();	
	update();
}

void AutoPrinter::OnPaperWidthChange( double dWidth )
{
	m_bPaperWidth = dWidth;
	m_pSettings->setValue(SETTING_CONFIG_PAPER_WIDTH, dWidth);
}

void AutoPrinter::OnPaperHeightChange(double dHeight)
{
	m_bPaperHeight = dHeight;
	m_pSettings->setValue(SETTING_CONFIG_PAPER_HEIGHT, dHeight);
}

void AutoPrinter::OnChangeWaterMarkFont()
{
	QFontDialog fontDialog;
	bool bRet;
	m_waterMarkFont = fontDialog.getFont(&bRet, m_waterMarkFont, this);

	if (bRet)
	{
		m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	}
}

void AutoPrinter::OnChangeWaterMarkFontColor()
{
	QColorDialog colorDialog;
	m_waterMarkColor = colorDialog.getColor(m_waterMarkColor);
	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
}

void AutoPrinter::LoadSettings()
{
	// TODO:	
}

void AutoPrinter::OnWaterMarkEnable( int state )
{
	if (state == Qt::Checked)
		ui.gpBoxWaterMarkSetting->setEnabled(true);
	else
		ui.gpBoxWaterMarkSetting->setEnabled(false);

	m_pMaskReviewWidget->SetDisplayImage(GetFramePreviewImage());
	m_pMaskReviewWidget->update();
}
