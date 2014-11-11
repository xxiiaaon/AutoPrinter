#include "autoprinter.h"
#include <QtGui>

//====================================================================================
class CombineImageMaskReview : public QWidget
{
	Q_OBJECT
public:
	CombineImageMaskReview(const QString &strImage)
		: QWidget()
		, m_imgTemplate(strImage)
		, m_nPosX(0)
		, m_nPosY(0)
	{
	}

	QSize GetTemplateImageSize() const
	{
		return m_imgTemplate.size();
	}

	void SetMaskPosX(int posX)
	{
		m_nPosX = posX;
	}
	
	void SetMaskPosY(int posY)
	{
		m_nPosY = posY;
	}

	void SetMaskSize(const QSize &size)
	{
		m_sizeMask.setWidth(size.width());
		m_sizeMask.setHeight(size.height());
	}

protected:
	virtual void paintEvent(QPaintEvent *event)
	{
		QImage imgPreview(m_imgTemplate.size(), QImage::Format_ARGB32_Premultiplied);

		QPainter painterPre(&imgPreview);
		QBrush whiteBrush(Qt::white);
		painterPre.drawImage(QPoint(0, 0), m_imgTemplate);
		painterPre.fillRect(m_nPosX, m_nPosY, m_sizeMask.width(), m_sizeMask.height(), whiteBrush);
		painterPre.end();

		QPainter painter(this);
		painter.drawImage(QPoint(0,0), imgPreview);
		painter.end();

		QWidget::paintEvent(event);
	}
	
private:
	QImage m_imgTemplate;
	int m_nPosX;
	int m_nPosY;
	QSize m_sizeMask;
};

//====================================================================================
AutoPrinter::AutoPrinter(QWidget *parent, Qt::WFlags flags)
	: QMainWindow(parent, flags)
	, m_FgImgPos(0, 0)
	, m_FgMaskSize(800, 600)
	, m_pMaskReviewWidget(NULL)
{
	ui.setupUi(this);
	ui.ImgHorzPosSlider->setRange(0, 100);
	ui.ImgHorzPosSlider->setValue(0);
	ui.ImgHorzPosSlider->setTickInterval(1);
	connect(ui.ImgHorzPosSlider, SIGNAL(valueChanged(int)), this, SLOT(OnPosHorizontalChange()));
	connect(ui.ImgVertPosSlider, SIGNAL(valueChanged(int)), this, SLOT(OnPosVerticalChange()));

	// Template File, Backup Dir, Output Dir, Scan Dir Path Select buttons.
	connect(ui.btnSelectScanPath, SIGNAL(pressed()), this, SLOT(OnSelectScanDir()));
	connect(ui.btnSelectBackupPath, SIGNAL(pressed()), this, SLOT(OnSelectBackupDir()));
	connect(ui.btnSelectOutputPath, SIGNAL(pressed()), this, SLOT(OnSelectOutputDir()));
	connect(ui.btnSelectTemplate, SIGNAL(pressed()), this, SLOT(OnSelectTemplatePath()));

	// Start, Stop, Save Setting function buttons.
	connect(ui.btnStart, SIGNAL(pressed()), this, SLOT(OnMonitorFolderStart()));
	connect(ui.btnStop, SIGNAL(pressed()), this, SLOT(OnMonitorFolderStop()));
	connect(ui.btnSaveSetting, SIGNAL(pressed()), this, SLOT(OnSaveSettings()));
}

AutoPrinter::~AutoPrinter()
{

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
	outputPainter.drawImage(m_FgImgPos, imgMask);
	outputPainter.end();

	QPainter painter(this);
	painter.drawImage(QPoint(0, 0), imgOutput);
	painter.end();
	QMainWindow::paintEvent(event);	
}

void AutoPrinter::OnPosHorizontalChange()
{
	m_FgImgPos.setX(ui.ImgHorzPosSlider->value());
	update();
}

void AutoPrinter::OnPosVerticalChange()
{
	m_FgImgPos.setY(ui.ImgVertPosSlider->value());
	update();
}

void AutoPrinter::OnSelectScanDir()
{
	QFileDialog fileDialog(this);
	if (!m_strScanDir.isEmpty())
		fileDialog.setDirectory(m_strScanDir);
	m_strScanDir = fileDialog.getExistingDirectory();

	ui.lnEdtScanDir->setText(m_strScanDir);
}

void AutoPrinter::OnSelectBackupDir()
{
	QFileDialog fileDialog(this);
	if (!m_strBackupDir.isEmpty())
		fileDialog.setDirectory(m_strBackupDir);
	m_strBackupDir = fileDialog.getExistingDirectory();

	ui.lnEdtBackupDir->setText(m_strBackupDir);
}

void AutoPrinter::OnSelectOutputDir()
{
	QFileDialog fileDialog(this);
	if (!m_strOutputDir.isEmpty())
		fileDialog.setDirectory(m_strOutputDir);
	m_strOutputDir = fileDialog.getExistingDirectory();

	ui.lnEdtOutputDir->setText(m_strOutputDir);
}

void AutoPrinter::OnSelectTemplatePath()
{
	QFileDialog fileDialog(this);
	if (!m_strTemplateFile.isEmpty())
		fileDialog.setDirectory(m_strTemplateFile);
	m_strTemplateFile = fileDialog.getOpenFileName();

	ui.lnEdtTemplate->setText(m_strTemplateFile);
}

void AutoPrinter::OnMonitorFolderStart()
{
	// TODO:
}

void AutoPrinter::OnMonitorFolderStop()
{
	// TODO:
}

void AutoPrinter::OnSaveSettings()
{
	// TODO:
}

void AutoPrinter::InitTemplatePreview()
{
	if (m_pMaskReviewWidget == NULL)
		m_pMaskReviewWidget = new CombineImageMaskReview(m_strTemplateFile);

	QSize size = m_pMaskReviewWidget->GetTemplateImageSize();

	ui.ImgHorzPosSlider->setRange(0, size.width() - ui.spBoxMaskWeight->value);
}
