/*=========================================================================

Library:   UltrasoundSpectroscopyRecorder

Copyright 2010 Kitware Inc. 28 Corporate Drive,
Clifton Park, NY, 12065, USA.

All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

=========================================================================*/

#include "myWindow.hxx"
#include "vtkImageViewer.h"
#include "vtkRenderWindowInteractor.h"

#include "ui_UltrasoundSpectroscopyRecorder.h"

#include <iostream>
#include <QtGui>

#include "PlusTrackedFrame.h"
#include "vtkPlusTrackedFrameList.h"
#include <vtkSmartPointer.h>
#include <vtkImageViewer2.h>
#include <vtkDICOMImageReader.h>
#include <vtkPlusBuffer.h>
#include "vtkPlusSequenceIO.h"
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <QVTKWidget.h>
#include <QFileDialog>
#include <vtkInteractorStyleImage.h>

// Interson Array probe
const double IntersonFrequencies[] = { 5.0, 7.5, 10.0 };
// Interson probe
//const double frequencies[] = { 2.5, 3.5, 5.0 };

myWindow::myWindow( QWidget *parent )
  :QMainWindow( parent ), ui( new Ui::MainWindow ), recordedFrames( NULL ), start( TRUE ), failRestart( FALSE ), timerHits( 0 )
{
  LOG_INFO( "Generate the new Window" );

  //Setup the graphical layout on this current Widget
  ui->setupUi( this );

  this->setWindowTitle( "Ultrasound Spectroscopy Recorder" );

  //Links buttons and actions
  connect( ui->pushButton_quit, SIGNAL( clicked() ), this, SLOT( ActionQuit() ) );
  connect( ui->pushButton_start, SIGNAL( clicked() ), this, SLOT( ActionStart() ) );
  connect( ui->pushButton_display, SIGNAL( clicked() ), this, SLOT( ActionDisplay() ) );

  connect( ui->pulseMin, SIGNAL( valueChanged( int ) ), this, SLOT( SetPulseMin() ) );
  connect( ui->pulseMax, SIGNAL( valueChanged( int ) ), this, SLOT( SetPulseMax() ) );
  connect( ui->pulseStep, SIGNAL( valueChanged( int ) ), this, SLOT( SetPulseStep() ) );

  ui->pushButton_start->setIcon( QPixmap( "../UltrasoundSpectroscopyRecorder/Resources/icon_Record.png" ) );
  //ui->pushButton_start->setFocus();

  // Create tracked frame list
  recordedFrames = vtkPlusTrackedFrameList::New();
  recordedFrames->SetValidationRequirements( REQUIRE_UNIQUE_TIMESTAMP );

  // Timer
  this->timer = new QTimer( this );
  this->timer->setSingleShot( true );
}

myWindow::~myWindow()
{
  LOG_INFO( "Destructor of myWindow is called" );
  delete timer;
  delete ui;
}

void myWindow::ActionDisplay()
{
  LOG_INFO( "Connect Interson probe" );
  if( IntersonDeviceWindow->Connect() != PLUS_SUCCESS )
    {
    LOG_ERROR( "Unable to connect to Interson Probe" );
    exit( EXIT_FAILURE );
    }
  this->record = false;
  LOG_INFO( "The frequency is set to 5MHz." );
  SetFrequencyMHz( IntersonFrequencies[ 0 ] );

  //Set the values of the probe for the display
  if( GetPulseMin() == 0 )
    {
    LOG_INFO( "The pulse is set by default to 20V." );
    this->IntersonDeviceWindow->SetPulseMin( 20 );
    }
  this->IntersonDeviceWindow->SetProbeFrequencyMhz( GetFrequency() );
  this->IntersonDeviceWindow->SetPulseVoltage( GetPulseMin() );
  this->IntersonDeviceWindow->StartRecording(); // start recording frames for the video

  // Show the live ultrasound image in a VTK renderer window
  vtkSmartPointer<vtkImageViewer2> viewer = vtkSmartPointer<vtkImageViewer2>::New();
  viewer->SetInputConnection( this->IntersonDeviceWindow->GetOutputPort() );   //set image to the render and window
  viewer->SetColorWindow( 255 );
  viewer->SetColorLevel( 127.5 );
  viewer->SetRenderWindow( ui->vtkRenderer->GetRenderWindow() ); //the main thing!
  viewer->Render();

  this->timer->setInterval( 100 );
  connect( timer, SIGNAL( timeout() ), this, SLOT( UpdateImage() ) );
  this->timer->start();
}

void myWindow::ActionStart()
{
  this->timer->stop();
  LOG_INFO( "Checking of the frequency and pulse values." );
  SetFrequencyMHz( IntersonFrequencies[ 0 ] );

  if( GetPulseMin() == 0 || GetPulseMax() == 0 )
    {
    LOG_ERROR( "The values must be strictly positive" );
    return;
    }
  if( GetPulseMax() < GetPulseMin() )
    {
    LOG_ERROR( "Error with the input arguments of the pulse : the minimum is bigger than the maximum." );
    return;
    }
  else if( GetPulseMax() != GetPulseMin() )
    {
    if( GetPulseStep() > ( GetPulseMax() - GetPulseMin() ) || GetPulseStep() == 0 )
      {
      LOG_ERROR( "Error with the input arguments of the pulse : the step is too big or equal to 0." );
      return;
      }
    }

  LOG_INFO( "Start Interson program." );

  // Change the function to be invoked on clicking on the now Stop button
  ui->pushButton_start->setText( "Stop" );
  ui->pushButton_start->setIcon( QPixmap( "../UltrasoundSpectroscopyRecorder/Resources/icon_Stop.png" ) );
  ui->pushButton_start->setFocus();
  disconnect( ui->pushButton_start, SIGNAL( clicked() ), this, SLOT( ActionStart() ) );
  connect( ui->pushButton_start, SIGNAL( clicked() ), this, SLOT( Stop() ) );

  QString outputFolder = QFileDialog::getExistingDirectory( this, "Choose directory to save the captured images", "C://" );
  if( outputFolder.isEmpty() || outputFolder.isNull() )
    {
    LOG_ERROR( "Folder not valid. Application stopped." )
      Stop();
    return;
    }
  SetOutputFolder( outputFolder );

  if( IntersonDeviceWindow->Connect() != PLUS_SUCCESS )
    {
    LOG_ERROR( "Unable to connect to Interson Probe" );
    exit( EXIT_FAILURE );
    }
  this->record = true;
  this->start = true;

  // Set the initial default values
  this->SetPulseMin();
  this->SetPulseMax();
  this->SetPulseStep();
  this->IntersonDeviceWindow->SetProbeFrequencyMhz( IntersonDeviceWindow->GetFrequency() );
  this->IntersonDeviceWindow->SetPulseVoltage( IntersonDeviceWindow->GetPulseMin() );
  this->pulseValue = GetPulseMin();

  Sleep( 100 );
  this->IntersonDeviceWindow->StartRecording(); // start recording frames for the video
  Sleep( 100 );

  this->timer->setInterval( 100 );
  connect( timer, SIGNAL( timeout() ), this, SLOT( UpdateImage() ) );
  this->timer->start();
}

void myWindow::UpdateImage()
{
  this->timer->stop();
  unsigned long frameNumber = IntersonDeviceWindow->GetFrameNumber();
  static unsigned int previousFrame = 0;
  std::cout << "UpdateImage : Frame number = " << frameNumber << std::endl;
  timerHits++;

  if( record == true )
    {
    if( start == true ) // start the recording (initialize the pulse value)
      {
      pulseValue = GetPulseMin();
      previousFrame += 1;
      }
    start = false;

    if( frameNumber != 0 && frameNumber > previousFrame )
      {
      previousFrame = frameNumber;
      IntersonDeviceWindow->StopRecording();
      timerHits = 0;
      failRestart = false;
      AddTrackedFramesToList();

      // Update the pulse and the frequency of the probe
      if( pulseValue >= this->GetPulseMax() && GetFrequency() >= IntersonFrequencies[ 2 ] )
        {
        record = false;
        SaveTrackedFrames();
        Stop();
        return;
        }
      else
        {
        if( pulseValue >= GetPulseMax() )
          {
          pulseValue = GetPulseMin();
          if( GetFrequency() == IntersonFrequencies[ 0 ] )
            {
            SetFrequencyMHz( IntersonFrequencies[ 1 ] );
            }
          else if( GetFrequency() == IntersonFrequencies[ 1 ] )
            {
            SetFrequencyMHz( IntersonFrequencies[ 2 ] );
            }
          IntersonDeviceWindow->SetProbeFrequencyMhz( GetFrequency() );
          }
        else
          {
          pulseValue += GetPulseStep();
          if( pulseValue > GetPulseMax() )
            {
            pulseValue = GetPulseMax();
            }
          }
        IntersonDeviceWindow->SetPulseVoltage( pulseValue );
        }
      Sleep( 100 );
      this->IntersonDeviceWindow->StartRecording();
      Sleep( 100 );
      this->timer->setInterval( 100 );
      }
    else
      {
      if( timerHits >= 50 )
        {
        if( failRestart == false )
          {
          LOG_INFO( "No images received from the probe ! " );
          failRestart = true;
          timerHits = 0;
          DeconnectConnect();
          }
        else
          {
          LOG_ERROR( "Couldn't receive new frames from Interson probe. Recording stopped" );
          timerHits = 0;
          SaveTrackedFrames();
          Stop();
          return;
          }
        }
      }
    }
  else // display only
    {
    if( frameNumber != 0 && frameNumber > previousFrame )
      {
      previousFrame = frameNumber;
      timerHits = 0;
      ui->vtkRenderer->GetRenderWindow()->Render();
      }
    else
      {
      if( timerHits >= 50 )
        {
        timerHits = 0;
        DeconnectConnect();
        }
      }
    }
  this->timer->start();
}

void myWindow::DeconnectConnect()
{
  LOG_INFO( "Recording stopped and Interson probe disconnected" );
  IntersonDeviceWindow->StopRecording();
  Sleep( 100 );
  IntersonDeviceWindow->Disconnect();
  Sleep( 100 );
  LOG_INFO( "Reconnect to Interson probe" );
  if( IntersonDeviceWindow->Connect() != PLUS_SUCCESS )
    {
    LOG_ERROR( "Unable to connect to Interson Probe" );
    exit( EXIT_FAILURE );
    }
  this->IntersonDeviceWindow->SetProbeFrequencyMhz( GetFrequency() );
  this->IntersonDeviceWindow->SetPulseVoltage( GetPulseMin() );
  LOG_INFO( "Restart Interson probe" );
  this->IntersonDeviceWindow->StartRecording(); // start recording frames for the video
}

void myWindow::Stop()
{
  LOG_INFO( "Recording stopped" );
  this->timer->stop();
  disconnect( timer, SIGNAL( timeout() ), this, SLOT( UpdateImage() ) );

  IntersonDeviceWindow->StopRecording();
  Sleep( 100 );
  IntersonDeviceWindow->Disconnect();
  Sleep( 100 );
  recordedFrames->Clear();
  ui->pushButton_start->setText( "Start" );
  ui->pushButton_start->setIcon( QPixmap( "../UltrasoundSpectroscopyRecorder/Resources/icon_Record.png" ) );
  //ui->pushButton_start->setFocus();
  disconnect( ui->pushButton_start, SIGNAL( clicked() ), this, SLOT( Stop() ) );
  connect( ui->pushButton_start, SIGNAL( clicked() ), this, SLOT( ActionStart() ) );
}

void myWindow::ActionQuit()
{
  close();

  this->IntersonDeviceWindow->StopRecording();
  Sleep( 100 );
  this->IntersonDeviceWindow->Disconnect();
  Sleep( 100 );
}

void myWindow::SetIntersonDevice( vtkIntersonSDKCxxVideoSourceWindow* source )
{
  this->IntersonDeviceWindow = source;
  //get the values from the .ui and set them to internal IntersonDeviceWindow
  SetPulseMin();
  SetPulseMax();
  SetPulseStep();
}

void myWindow::SetPulseMin()
{
  this->IntersonDeviceWindow->SetPulseMin( this->ui->pulseMin->value() );
}

void myWindow::SetPulseMax()
{
  this->IntersonDeviceWindow->SetPulseMax( this->ui->pulseMax->value() );
}

void myWindow::SetPulseStep()
{
  this->IntersonDeviceWindow->SetPulseStep( this->ui->pulseStep->value() );
}

int myWindow::GetPulseMin() const
{
  //return this->ui->pulseMin->value();
  return this->IntersonDeviceWindow->GetPulseMin();
}

int myWindow::GetPulseMax() const
{
  return this->ui->pulseMax->value();
}

int myWindow::GetPulseStep() const
{
  return this->ui->pulseStep->value();
}

double myWindow::GetFrequency() const
{
  return this->IntersonDeviceWindow->GetFrequency();
}

void myWindow::SetFrequencyMHz( double frequency )
{
  this->IntersonDeviceWindow->SetFrequency( frequency );
}

void myWindow::SetOutputFolder( QString outputFolder )
{
  this->outputFolder = outputFolder;
}

QString myWindow::GetOutputFolder() const
{
  return this->outputFolder;
}

void myWindow::AddTrackedFramesToList()
{
  PlusTrackedFrame trackedFrame;
  vtkPlusChannel* aChannel_Rf( NULL );

  if( IntersonDeviceWindow->GetOutputChannelByName( aChannel_Rf, "RfVideoStream" ) != PLUS_SUCCESS || aChannel_Rf == NULL || aChannel_Rf->GetTrackedFrame( trackedFrame ) != PLUS_SUCCESS )
    {
    LOG_ERROR( "Failed to get tracked frame for the record of multi frequencies and pulses!" );
    return;
    }
  // Check if there are any valid transforms
  std::vector<PlusTransformName> transformNames;
  trackedFrame.GetCustomFrameTransformNameList( transformNames );
  bool validFrame = false;
  if( transformNames.size() == 0 )
    {
    validFrame = true;
    }
  else
    {
    for( std::vector<PlusTransformName>::iterator it = transformNames.begin(); it != transformNames.end(); ++it )
      {
      TrackedFrameFieldStatus status = FIELD_INVALID;
      trackedFrame.GetCustomFrameTransformStatus( *it, status );
      if( status == FIELD_OK )
        {
        validFrame = true;
        break;
        }
      }
    }
  if( !validFrame )
    {
    LOG_WARNING( "Unable to record tracked frame: All the tool transforms are invalid!" );
    return;
    }

    // Add tracked frame to the list
  if( recordedFrames->AddTrackedFrame( &trackedFrame, vtkPlusTrackedFrameList::SKIP_INVALID_FRAME ) != PLUS_SUCCESS )
    {
    LOG_WARNING( "Frame could not be added because validation failed!" );
    return;
    }
  std::string frequency = std::to_string( GetFrequency() );
  std::replace( frequency.begin(), frequency.end(), '.', '_' );
  frequency.erase( 3 );
  recordedFrames->GetTrackedFrame( recordedFrames->GetNumberOfTrackedFrames() - 1 )->SetCustomFrameField( "RecordInformation", frequency + "MHz-" + std::to_string( pulseValue ) + "V-" );
  std::cout << "New frame acquired for frequency = " << GetFrequency() << " MHz and power = " << pulseValue << " V" << std::endl;
}

void myWindow::SaveTrackedFrames()
{
  QString outputFolder = this->GetOutputFolder();

  if( recordedFrames->GetNumberOfTrackedFrames() > 0 )
    {
    std::string path;
    vtkPlusTrackedFrameList* currentFrame = vtkPlusTrackedFrameList::New();
    currentFrame->SetValidationRequirements( REQUIRE_UNIQUE_TIMESTAMP );

    for( unsigned int i = 0; i < recordedFrames->GetNumberOfTrackedFrames(); i++ )
      {
      path = outputFolder.toStdString() + "\\VideoBufferMetafile_Rfmode-" + recordedFrames->GetTrackedFrame( i )->GetCustomFrameField( "RecordInformation" );
      std::string defaultFileName = path + vtksys::SystemTools::GetCurrentDateTime( "%Y%m%d_%H%M%S" ) + ".nrrd";

      PlusTrackedFrame* essai = recordedFrames->GetTrackedFrame( i );
      if( currentFrame->AddTrackedFrame( essai, vtkPlusTrackedFrameList::SKIP_INVALID_FRAME ) != PLUS_SUCCESS )
        {
        LOG_WARNING( "Frame could not be added because validation failed!" );
        return;
        }

      WriteToFile( QString( defaultFileName.c_str() ), currentFrame );

      LOG_INFO( "Captured tracked frame list saved into '" << defaultFileName << "'" );
      currentFrame->Clear();
      }
    recordedFrames->Clear();
    }
  else
    {
    LOG_ERROR( "Impossible to capture the tracked frame list : Number of tracked frames <= 0" );
    }
}


void myWindow::WriteToFile( const QString& aFilename, vtkPlusTrackedFrameList* currentFrame )
{
  if( aFilename.isEmpty() )
    {
    LOG_ERROR( "Writing sequence to metafile failed: output file name is empty" );
    return;
    }

  QApplication::setOverrideCursor( QCursor( Qt::BusyCursor ) );

  // Actual saving
  if( vtkPlusSequenceIO::Write( aFilename.toLatin1().constData(), currentFrame, US_IMG_ORIENT_FM ) != PLUS_SUCCESS )
    {
    LOG_ERROR( "Failed to save tracked frames to sequence metafile!" );
    return;
    }

  QString result = "File saved to\n" + aFilename;

  currentFrame->Clear();

  QApplication::restoreOverrideCursor();
}