#include "Profiler.h"
#include "CurveData.h"
#include "GenerateModes.h"
#include "ProfilePlotStyles.h"
#include "Data/Clips.h"
#include "Data/DataLoader.h"
#include "Data/Settings.h"
#include "Data/LinkableImpl.h"
#include "Data/Image/Controller.h"
#include "Data/Image/DataSource.h"
#include "Data/Image/Layer.h"
#include "Data/Error/ErrorManager.h"
#include "Data/Util.h"
#include "Data/Plotter/LegendLocations.h"
#include "Data/Plotter/Plot2DManager.h"
#include "Data/Plotter/LineStyles.h"
#include "Data/Profile/Fit/ProfileFitService.h"
#include "Data/Profile/Render/ProfileRenderService.h"
#include "Data/Profile/ProfileStatistics.h"
#include "Data/Units/UnitsFrequency.h"
#include "Data/Units/UnitsIntensity.h"
#include "Data/Units/UnitsSpectral.h"
#include "Plot2D/Plot2DGenerator.h"
#include "CartaLib/Hooks/Plot2DResult.h"
#include "CartaLib/Hooks/ConversionIntensityHook.h"
#include "CartaLib/Hooks/ConversionSpectralHook.h"
#include "CartaLib/Hooks/Fit1DHook.h"
#include "CartaLib/Hooks/ProfileHook.h"
#include "CartaLib/AxisInfo.h"
#include "CartaLib/ProfileInfo.h"
#include "State/UtilState.h"
#include "Globals.h"
#include "PluginManager.h"
#include <QtCore/qmath.h>
#include <QTime>
#include <QDebug>

namespace Carta {

namespace Data {

const QString Profiler::CLASS_NAME = "Profiler";
const QString Profiler::AXIS_UNITS_BOTTOM = "axisUnitsBottom";
const QString Profiler::AXIS_UNITS_LEFT = "axisUnitsLeft";
const QString Profiler::CURVES = "curves";
const QString Profiler::CURVE_SELECT = "selectCurve";
const QString Profiler::FIT_CENTER = "center";
const QString Profiler::FIT_PEAK = "peak";
const QString Profiler::FIT_FBHW = "fbhw";
const QString Profiler::FIT_CENTER_PIXEL = "centerPixel";
const QString Profiler::FIT_PEAK_PIXEL = "peakPixel";
const QString Profiler::FIT_FBHW_PIXEL = "fbhwPixel";
const QString Profiler::FIT_STATISTICS = "fitStats";
const QString Profiler::GAUSS_COUNT = "gaussCount";
const QString Profiler::GEN_MODE = "genMode";
const QString Profiler::GRID_LINES = "gridLines";
const QString Profiler::HEURISTICS = "heuristics";
const QString Profiler::IMAGES = "images";
const QString Profiler::INITIAL_GUESSES = "fitGuesses";
const QString Profiler::LEGEND_LOCATION = "legendLocation";
const QString Profiler::LEGEND_EXTERNAL = "legendExternal";
const QString Profiler::LEGEND_SHOW = "legendShow";
const QString Profiler::LEGEND_LINE = "legendLine";
const QString Profiler::MANUAL_GUESS = "manualGuess";
const QString Profiler::PLOT_WIDTH = "plotWidth";
const QString Profiler::PLOT_HEIGHT = "plotHeight";
const QString Profiler::PLOT_LEFT = "plotLeft";
const QString Profiler::PLOT_TOP = "plotTop";
const QString Profiler::POLY_DEGREE = "polyDegree";
const QString Profiler::REGIONS = "regions";
const QString Profiler::SHOW_GUESSES = "showGuesses";
const QString Profiler::SHOW_MEAN_RMS = "showMeanRMS";
const QString Profiler::SHOW_PEAK_LABELS = "showPeakLabels";
const QString Profiler::SHOW_RESIDUALS = "showResiduals";
const QString Profiler::SHOW_STATISTICS = "showStats";
const QString Profiler::SHOW_TOOLTIP = "showToolTip";
const QString Profiler::TOOL_TIPS = "toolTips";
const QString Profiler::ZOOM_BUFFER = "zoomBuffer";
const QString Profiler::ZOOM_BUFFER_SIZE = "zoomBufferSize";
const QString Profiler::ZOOM_MIN = "zoomMin";
const QString Profiler::ZOOM_MAX = "zoomMax";
const QString Profiler::ZOOM_MIN_PERCENT = "zoomMinPercent";
const QString Profiler::ZOOM_MAX_PERCENT = "zoomMaxPercent";
const int Profiler::ERROR_MARGIN = 0.000001;


class Profiler::Factory : public Carta::State::CartaObjectFactory {
public:
    Carta::State::CartaObject * create (const QString & path, const QString & id){
        return new Profiler (path, id);
    }
};

bool Profiler::m_registered =
        Carta::State::ObjectManager::objectManager()->registerClass ( CLASS_NAME, new Profiler::Factory());

UnitsSpectral* Profiler::m_spectralUnits = nullptr;
UnitsIntensity* Profiler::m_intensityUnits = nullptr;
GenerateModes* Profiler::m_generateModes = nullptr;
ProfileStatistics* Profiler::m_stats = nullptr;


QList<QColor> Profiler::m_curveColors = {Qt::blue, Qt::green, Qt::black, Qt::cyan,
        Qt::magenta, Qt::yellow, Qt::gray };



using Carta::State::UtilState;
using Carta::State::StateInterface;
using Carta::Plot2D::Plot2DGenerator;

Profiler::Profiler( const QString& path, const QString& id):
            CartaObject( CLASS_NAME, path, id ),
            m_linkImpl( new LinkableImpl( path )),
            m_preferences( nullptr),
            m_plotManager( new Plot2DManager( path, id ) ),
            m_legendLocations( nullptr),
            m_stateData( UtilState::getLookup(path, StateInterface::STATE_DATA) ),
            m_stateFit( UtilState::getLookup( path, CurveData::FIT)),
            m_stateFitStatistics( UtilState::getLookup( path, "fitStatistics")),
            m_renderService( new ProfileRenderService() ),
            m_fitService( new ProfileFitService() ){

    m_oldFrame = 0;
    m_currentFrame = 0;
    m_timerId = 0;

    qsrand(QTime::currentTime().msec());
    connect( m_renderService.get(),
            SIGNAL(profileResult(const Carta::Lib::Hooks::ProfileResult&,int,const QString&,bool,std::shared_ptr<Carta::Lib::Image::ImageInterface>)),
            this,
            SLOT(_profileRendered(const Carta::Lib::Hooks::ProfileResult&,int,const QString&,bool, std::shared_ptr<Carta::Lib::Image::ImageInterface>)));
    connect( m_fitService.get(),
               SIGNAL(fitResult(const std::vector<Carta::Lib::Hooks::FitResult>&)),
               this,
               SLOT(_fitFinished(const std::vector<Carta::Lib::Hooks::FitResult>&)));

    Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
    Settings* prefObj = objMan->createObject<Settings>();
    m_preferences.reset( prefObj );

    LegendLocations* legObj = objMan->createObject<LegendLocations>();
    m_legendLocations.reset( legObj );

    m_plotManager->setPlotGenerator( new Plot2DGenerator( Plot2DGenerator::PlotType::PROFILE) );
    m_plotManager->setTitleAxisY( "" );
    connect( m_plotManager.get(), SIGNAL(userSelection()), this, SLOT(_zoomToSelection()));
    connect( m_plotManager.get(), SIGNAL(userSelectionColor()), this, SLOT(_movieFrame()));
    connect( m_plotManager.get(), SIGNAL(cursorMove(double,double)),
            this, SLOT(_cursorUpdate(double,double)));
    connect( m_plotManager.get(), SIGNAL(plotSizeChanged()), this, SLOT(_plotSizeChanged()));

    _initializeStatics();
    _initializeDefaultState();
    _initializeCallbacks();

    _setErrorMargin();

    m_controllerLinked = false;
}


QString Profiler::addLink( CartaObject*  target){
    Controller* controller = dynamic_cast<Controller*>(target);
    bool linkAdded = false;
    QString result;
    if ( controller != nullptr ){
        if ( !m_controllerLinked ){
            linkAdded = m_linkImpl->addLink( controller );
            if ( linkAdded ){
                connect(controller, SIGNAL(dataChanged(Controller*)),
                        this , SLOT(_loadProfile(Controller*)));
                connect(controller, SIGNAL(frameChanged(Controller*, Carta::Lib::AxisInfo::KnownType)),
                        this, SLOT( _updateChannel(Controller*, Carta::Lib::AxisInfo::KnownType)));
                m_controllerLinked = true;
                _loadProfile( controller);
            }
        }
        else {
            CartaObject* obj = m_linkImpl->searchLinks( target->getPath());
            if ( obj != nullptr ){
                linkAdded = true;
            }
            else {
                result = "Profiler only supports linking to a single image source.";
            }
        }
    }
    else {
        result = "Profiler only supports linking to images";
    }
    return result;
}


void Profiler::_assignColor( std::shared_ptr<CurveData> curveData ){
    //First go through list of fixed colors & see if there is one available.
    int fixedColorCount = m_curveColors.size();
    int curveCount = m_plotCurves.size();
    bool colorAssigned = false;
    for ( int i = 0; i < fixedColorCount; i++ ){
        bool colorAvailable = true;
        QString fixedColorName = m_curveColors[i].name();
        for ( int j = 0; j < curveCount; j++ ){
            if ( m_plotCurves[j]->getColor().name() == fixedColorName ){
                colorAvailable = false;
                break;
            }
        }
        if ( colorAvailable ){
            curveData->setColor( m_curveColors[i] );
            colorAssigned = true;
            break;
        }
    }

    //If there is no color in the fixed list, assign a random one.
    if ( !colorAssigned ){
        const int MAX_COLOR = 255;
        int redAmount = qrand() % MAX_COLOR;
        int greenAmount = qrand() % MAX_COLOR;
        int blueAmount = qrand() % MAX_COLOR;
        QColor randomColor( redAmount, greenAmount, blueAmount );
        curveData->setColor( randomColor.name());
    }
}


void Profiler::_assignCurveName( std::shared_ptr<CurveData>& profileCurve ) const {
    QString name = profileCurve->getName();
    QString curveName = name;
    if ( name.trimmed().length() == 0 ){
        curveName = profileCurve->getNameImage();
        QString regionName = profileCurve->getNameRegion();
        if ( regionName.length() > 0 ){
            curveName = curveName + "(" + regionName + ")";
        }
    }

    //Make sure the name is unique among all the curves we have listed.
    int curveCount = m_plotCurves.size();
    bool duplicate = false;
    QString checkName = curveName;
    int iter = 1;
    do {
        duplicate = false;
        for ( int i = 0; i < curveCount; i++ ){
            QString cName = m_plotCurves[i]->getName();
            if ( cName == checkName ){
                duplicate = true;
                break;
            }
        }
        if ( duplicate ){
            checkName = curveName + "("+QString::number(iter)+")";
        }
        iter++;
    } while ( duplicate );
    profileCurve->setName( checkName );
}

void Profiler::_clearData(){
    m_plotManager->clearData();
    int curveSize = m_plotCurves.size();
    for ( int i = curveSize - 1; i>= 0; i-- ){
        QString curveName = m_plotCurves[i]->getName();
        profileRemove( curveName );
    }
}

std::vector<double> Profiler::_convertUnitsX( std::shared_ptr<CurveData> curveData,
        const QString& bottomUnit ) const {
    QString oldBottomUnit = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
    std::vector<double> converted = curveData->getValuesX();
    std::shared_ptr<Carta::Lib::Image::ImageInterface> dataSource = curveData->getSource();
    if ( ! bottomUnit.isEmpty() ){
        QString newUnit = _getUnitUnits( bottomUnit );
        QString oldUnit = _getUnitUnits( oldBottomUnit );
        if ( newUnit != oldUnit ){
            _convertX ( converted, dataSource, oldUnit, newUnit );
        }
    }
    return converted;
}

void Profiler::_convertX( std::vector<double>& converted,
        std::shared_ptr<Carta::Lib::Image::ImageInterface> dataSource,
        const QString& oldUnit, const QString& newUnit ) const {
    if ( dataSource ){
        auto result = Globals::instance()-> pluginManager()
                             -> prepare <Carta::Lib::Hooks::ConversionSpectralHook>(dataSource,
                                     oldUnit, newUnit, converted );
        auto lam = [&converted] ( const Carta::Lib::Hooks::ConversionSpectralHook::ResultType &data ) {
            converted = data;
        };
        try {
            result.forEach( lam );
        }
        catch( char*& error ){
            QString errorStr( error );
            ErrorManager* hr = Util::findSingletonObject<ErrorManager>();
            hr->registerError( errorStr );
        }
    }
}


std::vector<double> Profiler::_convertUnitsY( std::shared_ptr<CurveData> curveData, const QString& newUnit ) const {
    std::vector<double> converted = curveData->getValuesY();
    std::vector<double> plotDataX = curveData->getValuesX();
    QString leftUnit = m_state.getValue<QString>( AXIS_UNITS_LEFT );
    Controller* controller = _getControllerSelected();
    if ( controller ){
        std::shared_ptr<Carta::Lib::Image::ImageInterface> dataSource =
                curveData->getSource();
        if ( dataSource ){
            //First, we need to make sure the x-values are in Hertz.
            QString hertzKey = UnitsSpectral::NAME_FREQUENCY + "(" + UnitsFrequency::UNIT_HZ + ")";
            std::vector<double> hertzVals = _convertUnitsX( curveData, hertzKey );
            bool validBounds = false;
            std::pair<double,double> boundsY = m_plotManager->getPlotBoundsY( curveData->getName(), &validBounds );
            if ( validBounds ){
                QString maxUnit = m_plotManager->getAxisUnitsY();
                auto result = Globals::instance()-> pluginManager()
                                     -> prepare <Carta::Lib::Hooks::ConversionIntensityHook>(dataSource,
                                             leftUnit, newUnit, hertzVals, converted,
                                             boundsY.second, maxUnit );;

                auto lam = [&converted] ( const Carta::Lib::Hooks::ConversionIntensityHook::ResultType &data ) {
                    converted = data;
                };
                try {
                    result.forEach( lam );
                }
                catch( char*& error ){
                    QString errorStr( error );
                    ErrorManager* hr = Util::findSingletonObject<ErrorManager>();
                    hr->registerError( errorStr );
                }
            }
        }
    }
    return converted;
}

void Profiler::_cursorUpdate( double x, double y ){
    QString cursorText;
    int curveCount = m_plotCurves.size();
    double approxError = 1.0;
    //Find the curve with a point closest to x,y.
    for ( int i = 0; i < curveCount; i++ ){
        double error = 0;
        QString curveText = m_plotCurves[i]->getCursorText( x, y, &error );
        if ( !curveText.isEmpty() ){
            if ( error < approxError ){
                cursorText = curveText;
                approxError = error;
            }
        }
    }
    m_plotManager->setCursorText( cursorText );
}


int Profiler::_findCurveIndex( const QString& name ) const {
    int curveCount = m_plotCurves.size();
    int index = -1;
    for ( int i = 0; i < curveCount; i++ ){
        if ( m_plotCurves[i]->isMatch( name ) ){
            index = i;
            break;
        }
    }
    return index;
}



void Profiler::_fitFinished(const std::vector<Carta::Lib::Hooks::FitResult> & results){
    int resultCount = results.size();
    _updateFitStatistics( results );
    for ( int i = 0; i < resultCount; i++ ){
        Carta::Lib::Hooks::FitResult result = results[i];
        Carta::Lib::Fit1DInfo::StatusType statusType = result.getStatus();
        if ( statusType == Carta::Lib::Fit1DInfo::StatusType::COMPLETE ||
                statusType == Carta::Lib::Fit1DInfo::StatusType::PARTIAL ){
            std::vector<std::pair<double,double>> fitData = result.getData();

            int fitDataCount = fitData.size();
            std::vector<double> dataX( fitDataCount );
            std::vector<double> dataY( fitDataCount );
            double sum = 0;
            int count = 0;
            for ( int i = 0; i < fitDataCount; i++ ){
                dataX[i] = fitData[i].first;
                dataY[i] = fitData[i].second;
                if ( std::isfinite( dataY[i]) ){
                    sum = sum + dataY[i];
                    count++;
                }
            }
            double mean = sum / count;
            double rms = result.getRMS();
            m_plotManager->setMarkedRange( mean - rms, mean + rms );
            m_plotManager->setHLinePosition( mean );
            int curveIndex =  _findCurveIndex( result.getName() );
            if ( curveIndex >= 0 ){
                m_plotCurves[curveIndex ]->setFit( dataX, dataY );
                _updatePlotData();
            }

        }
        else if ( statusType == Carta::Lib::Fit1DInfo::StatusType::ERROR ){
            QString errorStr( "There was an error performing the fit." );
            ErrorManager* hr = Util::findSingletonObject<ErrorManager>();
            hr->registerError( errorStr );
        }
        else {
            qDebug() << "Status of fit="<<(int)(statusType);
        }
    }
}


void Profiler::_generateData( std::shared_ptr<Layer> layer, bool createNew ){
    QString layerName = layer->_getLayerName();
    int curveIndex = _findCurveIndex( layerName );
    std::shared_ptr<Carta::Lib::Image::ImageInterface> image = layer->_getImage();
    _generateData( image, curveIndex, layerName, createNew );
}

void Profiler::_generateFit( ){
    //Get the curves we will fit.
    int curveCount = m_plotCurves.size();
    int selectCount = 0;
    for ( int i = 0; i < curveCount; i++ ){
        if ( m_plotCurves[i]->isSelectedFit() ){
            selectCount++;
        }
    }
    if ( selectCount > 0 ) {
        std::vector<Carta::Lib::Fit1DInfo> fitInfos( selectCount );
        int polyCount = getPolyCount();
        int gaussCount = getGaussCount();
        if ( polyCount > 0 || gaussCount > 0 ){
            //If there are gaussians, make sure we have manual guesses in manual mode.
            //This may not be the case if there were previously no plot curves & hence,
            //no guesses.
            bool manualMode = isFitManualGuess();
            if ( gaussCount > 0 && manualMode ){
                int guessCount = getGuessCount();
                if ( guessCount != gaussCount ){
                    _makeInitialGuesses( gaussCount );
                    _resetFitGuessPixels();
                }
            }
            bool randomHeuristics = isRandomHeuristics();

            int j = 0;
            for ( int i = 0; i < curveCount; i++ ){
                if ( m_plotCurves[i]->isSelectedFit() ){
                    //Store the parameters used to fit the curve so that we can
                    //restore the state if we need to.
                    m_plotCurves[i]->setFitParams( m_stateFit.toString( CurveData::FIT) );
                    std::vector<double > curveData = m_plotCurves[i]->getValuesY();
                    QString name = m_plotCurves[i]->getName();
                    fitInfos[j].setId( name );
                    fitInfos[j].setData( curveData );
                    fitInfos[j].setPolyDegree( polyCount );
                    fitInfos[j].setGaussCount( gaussCount );
                    fitInfos[j].setRandomHeuristics( randomHeuristics );
                    if ( manualMode ){
                        fitInfos[j].setInitialGaussianGuesses( getFitGuesses());
                    }
                    j++;
                }
            }
            m_fitService->fitProfile(fitInfos );
        }
     }
}


void Profiler::_generateData( std::shared_ptr<Carta::Lib::Image::ImageInterface> image,
        int curveIndex, const QString& layerName, bool createNew ){
    std::vector < int > pos( image-> dims().size(), 0 );
    int axis = _getExtractionAxisIndex( image );
    if ( axis >= 0 ){
        //Profiles::PrincipalAxisProfilePath path( axis, pos );

        Carta::Lib::ProfileInfo profInfo;
        if ( curveIndex >= 0 ){
            profInfo = m_plotCurves[curveIndex]->getProfileInfo();
        }
        QString bottomUnits = getAxisUnitsBottom();

        profInfo.setSpectralUnit( _getUnitUnits( bottomUnits) );
        QString typeStr = _getUnitType( bottomUnits );
        if ( typeStr == UnitsSpectral::NAME_FREQUENCY ){
            typeStr = "";
        }
        profInfo.setSpectralType( typeStr );
        Carta::Lib::RegionInfo regionInfo;
        m_renderService->renderProfile(image, regionInfo, profInfo, curveIndex, layerName, createNew );

    }
}

std::vector<std::tuple<double,double,double> >
Profiler::_generateFitGuesses( int count, bool random ){
    CARTA_ASSERT( count >= 0 );
    std::vector<std::tuple<double,double,double> > guesses(count);

    //Set up uniformly spaced guesses based on the first curve that has been
    //selected to fit.
    double xmin = 0;
    double xmax = 10;
    double ymin = 0;
    double ymax = 10;
    double xRange = xmax - xmin;
    double xStep = xRange / (count + 1);
    double yDecrease = .1;
    const double FBHW_MULT = 0.45;
    double fbhw = FBHW_MULT * xRange / (count + 1);
    int curveCount = m_plotCurves.size();
    for ( int i = 0; i < curveCount; i++ ){
        if ( m_plotCurves[i]->isSelectedFit() ){
            m_plotCurves[i]->getMinMax(&xmin, &xmax, &ymin, &ymax );
            xRange = xmax - xmin;
            xStep = xRange / (count + 1);
            fbhw = FBHW_MULT * xRange / (count + 1);
            break;
        }
    }
    //Store the guesses.
    double peak = ymax;
    for ( int i = 0; i < count; i++ ){
        double center = xmin + xStep * (i + 1);
        if ( random ){
            int randomIndex = qrand() % ((int)(xRange));
            center = randomIndex + xmin;
        }
        //Go down a fixed percentile each time.
        peak = peak * qPow(yDecrease,i);
        if ( random ){
            //Weighted average of the minimum and maximum.
            peak = 0.75 * ymax + 0.25 * ymin;
        }
        double endDist = qMin( center - xmin, xmax -center );
        double fbhwFit = qMin( fbhw, endDist);
        guesses[i] = std::tuple<double,double,double>( center, peak, fbhwFit );
    }
    return guesses;
}


QString Profiler::getAxisUnitsBottom() const {
    QString bottomUnits = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
    return bottomUnits;
}


Controller* Profiler::_getControllerSelected() const {
    //We are only supporting one linked controller.
    Controller* controller = nullptr;
    int linkCount = m_linkImpl->getLinkCount();
    for ( int i = 0; i < linkCount; i++ ){
        CartaObject* obj = m_linkImpl->getLink(i );
        Controller* control = dynamic_cast<Controller*>( obj);
        if ( control != nullptr){
            controller = control;
            break;
        }
    }
    return controller;
}

std::pair<double,double> Profiler::_getCurveRangeX() const {
    double maxValue = -1 * std::numeric_limits<double>::max();
    double minValue = std::numeric_limits<double>::max();
    int curveCount = m_plotCurves.size();
    for ( int i = 0; i < curveCount; i++ ){
        double curveMinValue = minValue;
        double curveMaxValue = maxValue;
        double yMin = minValue;
        double yMax = maxValue;
        m_plotCurves[i]->getMinMax( &curveMinValue,&curveMaxValue,&yMin,&yMax);
        if ( curveMinValue < minValue ){
            minValue = curveMinValue;
        }
        if ( curveMaxValue > maxValue ){
            maxValue = curveMaxValue;
        }
    }
    return std::pair<double,double>( minValue, maxValue );
}


std::vector<std::shared_ptr<Layer> > Profiler::_getDataForGenerateMode( Controller* controller) const {
    QString generateMode = m_state.getValue<QString>( GEN_MODE );
    std::vector<std::shared_ptr<Layer> > dataSources;
    if ( m_generateModes->isCurrent( generateMode ) ){
        std::shared_ptr<Layer> dataSource = controller->getLayer();
        if ( dataSource ){
            dataSources.push_back( dataSource );
        }
    }
    else if ( m_generateModes->isAll( generateMode ) ){
        dataSources = controller->getLayers();
    }
    else if ( m_generateModes->isAllExcludeSingle( generateMode ) ){
        std::vector<std::shared_ptr<Layer> > dSources = controller->getLayers();
        int dCount = dSources.size();
        for ( int i = 0; i < dCount; i++ ){
            int specCount = dSources[i]->_getFrameCount( Carta::Lib::AxisInfo::KnownType::SPECTRAL );
            if ( specCount > 1 ){
                dataSources.push_back( dSources[i]);
            }
        }
    }
    return dataSources;
}


int Profiler::_getExtractionAxisIndex( std::shared_ptr<Carta::Lib::Image::ImageInterface> image ) const {
    int axis = Util::getAxisIndex( image, Carta::Lib::AxisInfo::KnownType::SPECTRAL );
    if ( axis < 0 ){
        //See if it has a tabular axis.
        axis = Util::getAxisIndex( image, Carta::Lib::AxisInfo::KnownType::TABULAR );
    }
    if ( axis < 0 ){
        //See if it has a linear axis.
        axis = Util::getAxisIndex( image, Carta::Lib::AxisInfo::KnownType::LINEAR );
    }
    return axis;
}

std::vector<std::tuple<double,double,double> > Profiler::getFitGuesses(){
    QString guessKey = Carta::State::UtilState::getLookup( CurveData::FIT, INITIAL_GUESSES );
    int guessCount = m_stateFit.getArraySize( guessKey );
    std::vector<std::tuple<double,double,double> > guesses( guessCount );
    for ( int i = 0; i < guessCount; i++ ){
        QString indexKey = Carta::State::UtilState::getLookup( guessKey, i );
        QString centerKey = Carta::State::UtilState::getLookup( indexKey, FIT_CENTER );
        double center = m_stateFit.getValue<double>( centerKey );
        QString peakKey = Carta::State::UtilState::getLookup( indexKey, FIT_PEAK );
        double peak = m_stateFit.getValue<double>( peakKey );
        QString fbhwKey = Carta::State::UtilState::getLookup( indexKey, FIT_FBHW );
        double fbhw = m_stateFit.getValue<double>( fbhwKey );
        std::tuple<double,double,double> guess( center, peak, fbhw );
        guesses[i] = guess;
    }
    return guesses;
}

QString Profiler::_getFitStatusMessage( Carta::Lib::Fit1DInfo::StatusType statType) const{
    QString statusStr( "");
    if ( statType == Carta::Lib::Fit1DInfo::StatusType::NOT_DONE ){
        statusStr = "Not performed.";
    }
    if ( statType == Carta::Lib::Fit1DInfo::StatusType::ERROR ){
        statusStr = "There was an error computing the fit.";
    }
    if ( statType == Carta::Lib::Fit1DInfo::StatusType::PARTIAL ){
        statusStr = "Partially computed.";
    }
    if ( statType == Carta::Lib::Fit1DInfo::StatusType::COMPLETE ){
        statusStr = "Completed.";
    }
    return statusStr;
}


int Profiler::getGaussCount() const {
    QString key = Carta::State::UtilState::getLookup( CurveData::FIT, GAUSS_COUNT );
    return m_stateFit.getValue<int>( key );
}

int Profiler::getGuessCount() const {
    QString key = Carta::State::UtilState::getLookup( CurveData::FIT, INITIAL_GUESSES );
    return m_stateFit.getArraySize( key );
}


QString Profiler::_getLegendLocationsId() const {
    return m_legendLocations->getPath();
}


QList<QString> Profiler::getLinks() const {
    return m_linkImpl->getLinkIds();
}


int Profiler::getPolyCount() const {
    QString key = Carta::State::UtilState::getLookup( CurveData::FIT, POLY_DEGREE );
    return m_stateFit.getValue<int>( key );
}


QString Profiler::_getPreferencesId() const {
    return m_preferences->getPath();
}

QString Profiler::getStateString( const QString& sessionId, SnapshotType type ) const{
    QString result("");
    if ( type == SNAPSHOT_PREFERENCES ){
        StateInterface prefState( "");
        prefState.setValue<QString>(Carta::State::StateInterface::OBJECT_TYPE, CLASS_NAME );
        prefState.insertValue<QString>(Util::PREFERENCES, m_state.toString());
        prefState.insertValue<QString>( Settings::SETTINGS, m_preferences->getStateString(sessionId, type) );
        prefState.insertValue<QString>( CurveData::FIT, m_stateFit.toString());
        result = prefState.toString();
    }
    else if ( type == SNAPSHOT_LAYOUT ){
        result = m_linkImpl->getStateString(getIndex(), getSnapType( type ));
    }
    return result;
}

QString Profiler::_getUnitType( const QString& unitStr ){
    QString unitType = unitStr;
    int unitStart = unitStr.indexOf( "(");
    if ( unitStart >= 0 ){
        unitType = unitStr.mid( 0, unitStart );
    }
    return unitType;
}


QString Profiler::_getUnitUnits( const QString& unitStr ){
    QString strippedUnit = "";
    int unitStart = unitStr.indexOf( "(");
    if ( unitStart >= 0 ){
        int substrLength = unitStr.length() - unitStart - 2;
        if ( substrLength > 0){
            strippedUnit = unitStr.mid( unitStart + 1, substrLength );
        }
    }
    return strippedUnit;
}

double Profiler::getZoomMax() const {
    return m_stateData.getValue<double>( ZOOM_MAX );
}

double Profiler::getZoomMin() const {
    return m_stateData.getValue<double>( ZOOM_MIN );
}

double Profiler::getZoomMinPercent() const {
    return m_stateData.getValue<double>( ZOOM_MIN_PERCENT );
}

double Profiler::getZoomMaxPercent() const {
    return m_stateData.getValue<double>( ZOOM_MAX_PERCENT );
}


void Profiler::_initializeDefaultState(){
    //Data state is the curves
    m_stateData.insertArray( IMAGES, 0 );
    m_stateData.insertArray( REGIONS, 0 );
    m_stateData.insertArray( CURVES, 0 );
    m_stateData.insertValue<int>(CURVE_SELECT, 0 );
    m_stateData.insertValue<double>( ZOOM_MIN, 0 );
    m_stateData.insertValue<double>(ZOOM_MAX, 1);
    m_stateData.insertValue<double>(ZOOM_MIN_PERCENT, 0);
    m_stateData.insertValue<double>(ZOOM_MAX_PERCENT, 100 );
    m_stateData.insertValue<bool>(ZOOM_BUFFER, false );
    m_stateData.insertValue<double>( ZOOM_BUFFER_SIZE, 10 );
    m_stateData.flushState();

    //Default units
    QString bottomUnit = m_spectralUnits->getDefault();
    QString unitType = _getUnitType( bottomUnit );
    m_plotManager->setTitleAxisX( unitType );
    m_state.insertValue<QString>( AXIS_UNITS_BOTTOM, bottomUnit );
    m_state.insertValue<QString>( AXIS_UNITS_LEFT, m_intensityUnits->getDefault());
    m_state.insertValue<QString>(GEN_MODE, m_generateModes->getDefault());
    m_state.insertValue<bool>(TOOL_TIPS, false );


    //Legend
    bool external = true;
    QString legendLoc = m_legendLocations->getDefaultLocation( external );
    m_state.insertValue<QString>( LEGEND_LOCATION, legendLoc );
    m_state.insertValue<bool>( LEGEND_EXTERNAL, external );
    m_state.insertValue<bool>( LEGEND_SHOW, true );
    m_state.insertValue<bool>( LEGEND_LINE, true );

    //Plot
    m_state.insertValue<bool>(GRID_LINES, false );

    //Default Tab
    m_state.insertValue<int>( Util::TAB_INDEX, 2 );
    m_state.insertValue<bool>( SHOW_TOOLTIP, true );

    //Significant digits.
    m_state.insertValue<int>(Util::SIGNIFICANT_DIGITS, 6 );

    //Fit show/hide on display
    m_state.insertValue<bool>( SHOW_RESIDUALS, true );
    m_state.insertValue<bool>( SHOW_GUESSES, false );
    m_state.insertValue<bool>( SHOW_STATISTICS, true );
    m_state.insertValue<bool>( SHOW_MEAN_RMS, false );

    m_state.insertValue<bool>( SHOW_PEAK_LABELS, false );
    m_state.flushState();

    //Fit Parameters
    m_stateFit.insertValue<int>( PLOT_WIDTH, 0 );
    m_stateFit.insertValue<int>( PLOT_HEIGHT, 0 );
    m_stateFit.insertValue<int>( PLOT_LEFT, 0 );
    m_stateFit.insertValue<int>( PLOT_TOP, 0 );
    m_stateFit.insertObject( CurveData::FIT );
    QString gaussKey = Carta::State::UtilState::getLookup( CurveData::FIT, GAUSS_COUNT );
    m_stateFit.insertValue<int>( gaussKey, 0 );
    QString polyKey = Carta::State::UtilState::getLookup( CurveData::FIT, POLY_DEGREE );
    m_stateFit.insertValue<int>( polyKey, 0 );
    QString heurKey = Carta::State::UtilState::getLookup( CurveData::FIT, HEURISTICS );
    m_stateFit.insertValue<bool>( heurKey, true );
    QString manKey = Carta::State::UtilState::getLookup( CurveData::FIT, MANUAL_GUESS );
    m_stateFit.insertValue<bool>( manKey, false );
    QString guessesKey = Carta::State::UtilState::getLookup( CurveData::FIT, INITIAL_GUESSES );
    m_stateFit.insertArray( guessesKey, 0 );
    m_stateFit.flushState();

    m_stateFitStatistics.insertValue<QString>( FIT_STATISTICS, "" );
    m_stateFitStatistics.flushState();

}


void Profiler::_initializeCallbacks(){

    addCommandCallback( "registerLegendLocations", [=] (const QString & /*cmd*/,
            const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        QString result = _getLegendLocationsId();
        return result;
    });

    addCommandCallback( "registerPreferences", [=] (const QString & /*cmd*/,
            const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        QString result = _getPreferencesId();
        return result;
    });

    addCommandCallback( "resetInitialFitGuesses", [=] (const QString & /*cmd*/,
                const QString & /*params*/, const QString & /*sessionId*/) -> QString {
            resetInitialFitGuesses();
            return "";
        });


    addCommandCallback( "setAxisUnitsBottom", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::UNITS};

        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString unitStr = dataValues[*keys.begin()];
        QString result = setAxisUnitsBottom( unitStr );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setAxisUnitsLeft", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::UNITS};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString unitStr = dataValues[*keys.begin()];
        QString result = setAxisUnitsLeft( unitStr );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setFitManualGuesses", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
            QString result;
            std::set<QString> keys = {INITIAL_GUESSES};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString guessStr = dataValues[*keys.begin()];
            QStringList guessList = guessStr.split( " ");
            const int GUESS_SIZE = 3;
            int guessCount = guessList.size() / GUESS_SIZE;
            std::vector<std::tuple<int,int,int> > guesses( guessCount);
            QString errorMsg = "Initial fit manual guesses must be valid integers: "+params;
            for ( int i = 0; i < guessCount; i++ ){
                bool validCenter = false;
                bool validPeak = false;
                bool validFBHW = false;
                //Screen coordinates of a guess.
                int centerPixel = guessList[i*GUESS_SIZE].toInt(&validCenter );
                int peakPixel = guessList[i*GUESS_SIZE+1].toInt(&validPeak );
                int fbhwPixel = guessList[i*GUESS_SIZE+2].toInt(&validFBHW );
                if ( !validCenter || !validPeak || !validFBHW ){
                    result = errorMsg;
                    break;
                }
                guesses[i] = std::tuple<int,int,int>(centerPixel, peakPixel,fbhwPixel);
            }
            if ( result.isEmpty() ){
                setFitInitialGuessesPixels( guesses );
            }
            Util::commandPostProcess( result );
            return result;
        });


    addCommandCallback( "setManualGuess", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {MANUAL_GUESS};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString guessStr = dataValues[*keys.begin()];
        bool validBool = false;
        bool manualGuess = Util::toBool( guessStr, &validBool );
        if ( validBool ){
            setFitManualGuess( manualGuess );
        }
        else {
            result = "Whether or not to manually specify fit initial conditions must be true/false: " + params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setShowMeanRMS", [=] (const QString & /*cmd*/,
               const QString & params, const QString & /*sessionId*/) -> QString {
           QString result;
           std::set<QString> keys = {SHOW_MEAN_RMS};
           std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
           QString showStr = dataValues[*keys.begin()];
           bool validBool = false;
           bool show = Util::toBool( showStr, &validBool );
           if ( validBool ){
               setShowMeanRMS( show );
           }
           else {
               result = "Whether or not to show fit mean & RMS must be true/false: " + params;
           }
           Util::commandPostProcess( result );
           return result;
       });

    addCommandCallback( "setShowPeakLabels", [=] (const QString & /*cmd*/,
                  const QString & params, const QString & /*sessionId*/) -> QString {
              QString result;
              std::set<QString> keys = {SHOW_PEAK_LABELS};
              std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
              QString showStr = dataValues[*keys.begin()];
              bool validBool = false;
              bool show = Util::toBool( showStr, &validBool );
              if ( validBool ){
                  setShowPeakLabels( show );
              }
              else {
                  result = "Whether or not to show peak labels must be true/false: " + params;
              }
              Util::commandPostProcess( result );
              return result;
          });

    addCommandCallback( "setShowResiduals", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {SHOW_RESIDUALS};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString residualStr = dataValues[*keys.begin()];
        bool validBool = false;
        bool residuals = Util::toBool( residualStr, &validBool );
        if ( validBool ){
            setShowFitResiduals( residuals );
        }
        else {
            result = "To show fit residuals must be true/false: " + params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setShowGuesses", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
            QString result;
            std::set<QString> keys = {SHOW_GUESSES};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString guessStr = dataValues[*keys.begin()];
            bool validBool = false;
            bool guesses = Util::toBool( guessStr, &validBool );
            if ( validBool ){
                result = setShowFitGuesses( guesses );
            }
            else {
                result = "Whether or not to show fit manual guesses must be true/false: " + params;
            }
            Util::commandPostProcess( result );
            return result;
        });

    addCommandCallback( "setShowStatistics", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
            QString result;
            std::set<QString> keys = {SHOW_STATISTICS};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString statStr = dataValues[*keys.begin()];
            bool validBool = false;
            bool stats = Util::toBool( statStr, &validBool );
            if ( validBool ){
                setShowFitStatistics( stats );
            }
            else {
                result = "To show fit residuals must be true/false: " + params;
            }
            Util::commandPostProcess( result );
            return result;
        });

    addCommandCallback( "setZoomBufferSize", [=] (const QString & /*cmd*/,
                      const QString & params, const QString & /*sessionId*/) -> QString {
           QString result;
           std::set<QString> keys = {ZOOM_BUFFER_SIZE};
           std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
           QString zoomBufferStr = dataValues[*keys.begin()];
           bool validInt = false;
           double zoomBuffer = zoomBufferStr.toInt( &validInt );
           if ( validInt ){
               result = setZoomBufferSize( zoomBuffer );
           }
           else {
               result = "Invalid zoom buffer size: " + params+" must be a valid integer.";
           }
           Util::commandPostProcess( result );
           return result;
       });

    addCommandCallback( "setRandomHeuristics", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {HEURISTICS};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString heuristicsStr = dataValues[*keys.begin()];
        bool validBool = false;
        bool heuristics = Util::toBool(heuristicsStr, &validBool );
        if ( validBool ){
            setRandomHeuristics( heuristics );
        }
        else {
            result = "Whether or not to use random heuristics must be true/false: " + params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setZoomBuffer", [=] (const QString & /*cmd*/,
                  const QString & params, const QString & /*sessionId*/) -> QString {
           QString result;
          std::set<QString> keys = {ZOOM_BUFFER};
          std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
          QString zoomBufferStr = dataValues[*keys.begin()];
          bool validBool = false;
          bool zoomBuffer = Util::toBool(zoomBufferStr, &validBool );
          if ( validBool ){
              setZoomBuffer( zoomBuffer );
          }
          else {
              result = "Use zoom buffer must be true/false: " + params;
          }
          Util::commandPostProcess( result );
          return result;
      });

    addCommandCallback( "setCurveName", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
            std::set<QString> keys = {Util::NAME, "oldName"};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString nameStr = dataValues[Util::NAME];
            QString idStr = dataValues["oldName"];
            QString result = setCurveName( idStr, nameStr );
            Util::commandPostProcess( result );
            return result;
        });

    addCommandCallback( "setFitCurves", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
                std::set<QString> keys = {"fitCurves"};
                std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
                QString names = dataValues[*keys.begin()];
                QStringList nameList = names.split( ";");
                QString result = setFitCurves( nameList );
                Util::commandPostProcess( result );
                return result;
            });

    addCommandCallback( "setGenerationMode", [=] (const QString & /*cmd*/,
                    const QString & params, const QString & /*sessionId*/) -> QString {
                std::set<QString> keys = {"mode"};
                std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
                QString modeStr = dataValues[*keys.begin()];
                QString result = setGenerateMode( modeStr );
                Util::commandPostProcess( result );
                return result;
            });

    addCommandCallback( "setRestFrequency", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::NAME, CurveData::REST_FREQUENCY};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString nameStr = dataValues[Util::NAME];
        QString restFreqStr = dataValues[CurveData::REST_FREQUENCY];
        bool validDouble = false;
        double restFreq = restFreqStr.toDouble( &validDouble );
        QString result;
        if ( validDouble ){
            result = setRestFrequency( restFreq, nameStr );
        }
        else {
            result = "Rest frequency must be a valid number: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setRestUnit", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::NAME, CurveData::REST_UNIT_FREQ};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString nameStr = dataValues[Util::NAME];
        QString restUnits = dataValues[CurveData::REST_UNIT_FREQ];
        QString result = setRestUnits( restUnits, nameStr );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setRestUnitType", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::NAME, CurveData::REST_FREQUENCY_UNITS};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString nameStr = dataValues[Util::NAME];
        QString restUnitsStr = dataValues[CurveData::REST_FREQUENCY_UNITS];
        bool validBool = false;
        bool restUnitsFreq = Util::toBool( restUnitsStr, &validBool );
        QString result;
        if ( validBool ){
            result = setRestUnitType( restUnitsFreq, nameStr );
        }
        else {
            result = "Rest unit type frequency must be true/false: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "newProfile", [=] (const QString & /*cmd*/,
                    const QString & /*params*/, const QString & /*sessionId*/) -> QString {
                QString result = profileNew();
                Util::commandPostProcess( result );
                return result;
            });

    addCommandCallback( "copyProfile", [=] (const QString & /*cmd*/,
                        const QString & params, const QString & /*sessionId*/) -> QString {
                    std::set<QString> keys = {Util::NAME};
                    std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
                    QString nameStr = dataValues[Util::NAME];
                    QString result = profileCopy( nameStr );
                    Util::commandPostProcess( result );
                    return result;
                });

    addCommandCallback( "removeProfile", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::NAME};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString nameStr = dataValues[Util::NAME];
        QString result = profileRemove( nameStr );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "resetRestFrequency", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {Util::NAME};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString nameStr = dataValues[Util::NAME];
        QString result = resetRestFrequency( nameStr );
        Util::commandPostProcess( result );
        return result;
    });


    addCommandCallback( "setCurveColor", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {Util::RED, Util::GREEN, Util::BLUE, Util::NAME};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString redStr = dataValues[Util::RED];
        QString greenStr = dataValues[Util::GREEN];
        QString blueStr = dataValues[Util::BLUE];
        QString curveName = dataValues[Util::NAME];
        bool validRed = false;
        int redAmount = redStr.toInt( &validRed );
        bool validGreen = false;
        int greenAmount = greenStr.toInt( &validGreen );
        bool validBlue = false;
        int blueAmount = blueStr.toInt( &validBlue );
        if ( validRed && validGreen && validBlue ){
            QStringList resultList = setCurveColor( curveName, redAmount, greenAmount, blueAmount );
            result = resultList.join( ";");
        }
        else {
            result = "Please check that curve colors are integers: " + params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setGridLines", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
            std::set<QString> keys = {GRID_LINES};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString gridStr = dataValues[GRID_LINES];
            bool validBool = false;
            bool gridLines = Util::toBool( gridStr, &validBool );
            QString result;
            if ( validBool ){
                setGridLines( gridLines );
            }
            else {
                result = "Set toggling plot grid lines must be true/false: "+params;
            }
            Util::commandPostProcess( result );
            return result;
        });

    addCommandCallback( "setGaussCount", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {GAUSS_COUNT};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString countStr = dataValues[GAUSS_COUNT];
        bool validCount = false;
        int count = countStr.toInt( &validCount );
        if ( validCount ){
            result = setGaussCount( count );
        }
        else {
            result = "Profile fit gaussian count must be an integer: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLegendLocation", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {LEGEND_LOCATION};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString locationStr = dataValues[LEGEND_LOCATION];
        QString result = setLegendLocation( locationStr );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLegendExternal", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {LEGEND_EXTERNAL};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString externalStr = dataValues[LEGEND_EXTERNAL];
        bool validBool = false;
        bool externalLegend = Util::toBool( externalStr, &validBool );
        QString result;
        if ( validBool ){
            setLegendExternal( externalLegend );
        }
        else {
            result = "Setting the legend external to the plot must be true/false: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLegendShow", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {LEGEND_SHOW};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString showStr = dataValues[LEGEND_SHOW];
        bool validBool = false;
        bool show = Util::toBool( showStr, &validBool );
        QString result;
        if ( validBool ){
            setLegendShow( show );
        }
        else {
            result = "Set show legend must be true/false: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLegendLine", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {LEGEND_LINE};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString showStr = dataValues[LEGEND_LINE];
        bool validBool = false;
        bool show = Util::toBool( showStr, &validBool );
        QString result;
        if ( validBool ){
            setLegendLine( show );
        }
        else {
            result = "Set show legend line must be true/false: "+params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setLineStyle", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        std::set<QString> keys = {CurveData::STYLE, Util::NAME};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString lineStyle = dataValues[CurveData::STYLE];
        QString curveName = dataValues[Util::NAME];
        QString result = setLineStyle( curveName, lineStyle );
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setPolyDegree", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
            QString result;
            std::set<QString> keys = {POLY_DEGREE};
            std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
            QString countStr = dataValues[POLY_DEGREE];
            bool validCount = false;
            int count = countStr.toInt( &validCount );
            if ( validCount ){
                result = setPolyCount( count );
            }
            else {
                result = "Profile fit polynomial count must be an integer: "+params;
            }
            Util::commandPostProcess( result );
            return result;
        });

    addCommandCallback( "setPlotStyle", [=] (const QString & /*cmd*/,
               const QString & params, const QString & /*sessionId*/) -> QString {
           std::set<QString> keys = {CurveData::STYLE, Util::NAME};
           std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
           QString plotStyle = dataValues[CurveData::STYLE];
           QString curveName = dataValues[Util::NAME];
           QString result = setPlotStyle( curveName, plotStyle );
           Util::commandPostProcess( result );
           return result;
       });

    addCommandCallback( "setSignificantDigits", [=] (const QString & /*cmd*/,
                   const QString & params, const QString & /*sessionId*/) -> QString {
               QString result;
               std::set<QString> keys = {Util::SIGNIFICANT_DIGITS};
               std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
               QString digitsStr = dataValues[Util::SIGNIFICANT_DIGITS];
               bool validDigits = false;
               int digits = digitsStr.toInt( &validDigits );
               if ( validDigits ){
                   result = setSignificantDigits( digits );
               }
               else {
                   result = "Profile significant digits must be an integer: "+params;
               }
               Util::commandPostProcess( result );
               return result;
           });

    addCommandCallback( "setStatistic", [=] (const QString & /*cmd*/,
                   const QString & params, const QString & /*sessionId*/) -> QString {
               std::set<QString> keys = { CurveData::STATISTIC, Util::NAME};
               std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
               QString statStr = dataValues[CurveData::STATISTIC];
               QString curveName = dataValues[Util::NAME];
               QString result = setStatistic( statStr, curveName );
               Util::commandPostProcess( result );
               return result;
           });

    addCommandCallback( "setTabIndex", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QString result;
        std::set<QString> keys = {Util::TAB_INDEX};
        std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
        QString tabIndexStr = dataValues[Util::TAB_INDEX];
        bool validIndex = false;
        int tabIndex = tabIndexStr.toInt( &validIndex );
        if ( validIndex ){
            result = setTabIndex( tabIndex );
        }
        else {
            result = "Please check that the tab index is a number: " + params;
        }
        Util::commandPostProcess( result );
        return result;
    });

    addCommandCallback( "setZoomRange", [=] (const QString & /*cmd*/,
               const QString & params, const QString & /*sessionId*/) -> QString {
           QString result;
           std::set<QString> keys = {ZOOM_MIN, ZOOM_MAX};
           std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
           QString zoomMaxStr = dataValues[ZOOM_MAX];
           bool validRangeMax = false;
           double zoomMax = zoomMaxStr.toDouble( &validRangeMax );
           QString zoomMinStr = dataValues[ZOOM_MIN];
           bool validRangeMin = false;
           double zoomMin = zoomMinStr.toDouble( &validRangeMin );
           if ( !validRangeMax || !validRangeMin ){
               result = "Invalid profile range: " + params+"; bounds must be valid number(s).";
           }
           else {
               result = setZoomRange( zoomMin, zoomMax );
           }
           Util::commandPostProcess( result );
           return result;
       });

       addCommandCallback( "setZoomRangePercent", [=] (const QString & /*cmd*/,
                        const QString & params, const QString & /*sessionId*/) -> QString {
          QString result;

          std::set<QString> keys = {ZOOM_MIN_PERCENT, ZOOM_MAX_PERCENT};
          std::map<QString,QString> dataValues = Carta::State::UtilState::parseParamMap( params, keys );
          QString zoomMaxPercentStr = dataValues[ZOOM_MAX_PERCENT];
          bool validRangeMax = false;
          double zoomMaxPercent = zoomMaxPercentStr.toDouble( &validRangeMax );
          QString zoomMinPercentStr = dataValues[ZOOM_MIN_PERCENT];
          bool validRangeMin = false;
          double zoomMinPercent = zoomMinPercentStr.toDouble( &validRangeMin );

          if ( !validRangeMin || !validRangeMax ){
              result = "Invalid profile zoom percent range: " + params+"; must be valid number(s).";
          }
          else {
              result = setZoomRangePercent( zoomMinPercent, zoomMaxPercent );
          }
          Util::commandPostProcess( result );
          return result;
       });

       addCommandCallback( "zoomFull", [=] (const QString & /*cmd*/,
               const QString & /*params*/, const QString & /*sessionId*/) -> QString {
           QString result = setZoomRangePercent( 0, 100);
           Util::commandPostProcess( result );
           return result;
       });
}


void Profiler::_initializeStatics(){
    if ( m_spectralUnits == nullptr ){
        m_spectralUnits = Util::findSingletonObject<UnitsSpectral>();
    }
    if ( m_intensityUnits == nullptr ){
        m_intensityUnits = Util::findSingletonObject<UnitsIntensity>();
    }
    if ( m_generateModes == nullptr ){
        m_generateModes = Util::findSingletonObject<GenerateModes>();
    }
    if ( m_stats == nullptr ){
        m_stats = Util::findSingletonObject<ProfileStatistics>();
    }
}

bool Profiler::isFitManualGuess( ) const {
    QString key = Carta::State::UtilState::getLookup( CurveData::FIT, MANUAL_GUESS );
    return m_stateFit.getValue<bool>( key );
}


bool Profiler::isLinked( const QString& linkId ) const {
    bool linked = false;
    CartaObject* obj = m_linkImpl->searchLinks( linkId );
    if ( obj != nullptr ){
        linked = true;
    }
    return linked;
}


bool Profiler::isRandomHeuristics() const {
    QString key = Carta::State::UtilState::getLookup( CurveData::FIT, HEURISTICS );
    return m_stateFit.getValue<bool>( key );
}


void Profiler::_loadProfile( Controller* controller ){
    if( ! controller) {
        return;
    }
    _updateAvailableImages( controller );
    std::vector<std::shared_ptr<Layer> > layers = _getDataForGenerateMode( controller );
    m_plotManager->clearData();

    //Go through the old profiles and remove any that are no longer present.
    int curveCount = m_plotCurves.size();
    int dataCount = layers.size();
    QList<int> removeIndices;
    for ( int i = 0; i < curveCount; i++ ){
        QString imageName = m_plotCurves[i]->getNameImage();
        bool layerFound = false;
        for ( int j = 0; j < dataCount; j++ ){
            QString layerName = layers[j]->_getLayerName();
            if ( layerName == imageName ){
                layerFound = true;
                break;
            }
        }
        if ( !layerFound ){
            removeIndices.append( i );
        }
    }
    int removeCount = removeIndices.size();
    for ( int i = removeCount - 1; i >= 0; i-- ){
        m_plotCurves.removeAt( removeIndices[i] );
    }

    //Make profiles for any new data that has been loaded.
    bool generates = false;
    for ( int i = 0; i < dataCount; i++ ) {
        QString layerName = layers[i]->_getLayerName();
        int curveCount = m_plotCurves.size();
        int profileIndex = -1;
        for ( int j = 0; j < curveCount; j++ ){
            QString imageName = m_plotCurves[j]->getNameImage();
            if ( imageName == layerName ){
                profileIndex = j;
                break;
            }
        }
        if ( profileIndex < 0 ){
            generates = true;
            _generateData( layers[i]);
        }
    }
    _saveCurveState();
    //If we removed some curves but did not generate any new ones, the plot
    //needs to get updated (it will be updated automatically if a new curve is generated.
    if ( removeIndices.size() > 0 && !generates ){
        _updatePlotData();
    }
}



void Profiler::_movieFrame(){
    //Get the new frame from the plot
    bool valid = false;
    double xLocation = qRound( m_plotManager -> getVLinePosition(&valid));
    if ( valid ){
        //Need to convert the xLocation to a frame number.
        if ( m_plotCurves.size() > 0 ){
            std::vector<double> val(1);
            val[0] = xLocation;
            QString oldUnits = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
            QString basicUnit = _getUnitUnits( oldUnits );
            if ( !basicUnit.isEmpty() ){
                _convertX( val, m_plotCurves[0]->getSource(), basicUnit, "");
            }
            Controller* controller = _getControllerSelected();
            if ( controller && m_timerId == 0 ){
                int oldFrame = controller->getFrame( Carta::Lib::AxisInfo::KnownType::SPECTRAL );
                if ( oldFrame != val[0] ){
                    m_oldFrame = oldFrame;
                    m_currentFrame = val[0];
                    m_timerId = startTimer( 1000 );
                }
            }
        }
    }
}

void Profiler::_plotSizeChanged(){
    QSize plotSize = m_plotManager->getPlotSize();
    m_stateFit.setValue<int>(PLOT_WIDTH, plotSize.width());
    m_stateFit.setValue<int>(PLOT_HEIGHT, plotSize.height());
    QPointF upperLeft = m_plotManager->getPlotUpperLeft();
    m_stateFit.setValue<int>(PLOT_LEFT, upperLeft.x() );
    m_stateFit.setValue<int>(PLOT_TOP, upperLeft.y() );
    _resetFitGuessPixels();
}

QString Profiler::profileNew(){
    QString result;
    Controller* controller = _getControllerSelected();
    if ( controller){
        std::shared_ptr<Layer> layer = controller->getLayer();
        _generateData( layer, true );
    }
    else {
        result = "Could not generate a profile - no linked images.";
    }
    return result;
}

QString Profiler::profileCopy( const QString& baseName ){
    QString result;
    int curveIndex = _findCurveIndex( baseName );
    if ( curveIndex >= 0 ){
        //Create a new plot curve, and copy data.
        Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
        std::shared_ptr<CurveData> profileCurve( objMan->createObject<CurveData>() );
        profileCurve->copy( m_plotCurves[curveIndex]);
        _assignCurveName( profileCurve );
        m_plotCurves.append( profileCurve );
        _saveCurveState();
        _updatePlotData();
    }
    else {
        result = "Could not find the profile "+baseName+" to copy.";
    }
    return result;
}

QString Profiler::profileRemove( const QString& name ){
    QString result;
    int curveIndex = _findCurveIndex( name );
    if ( curveIndex >= 0 ){
        Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
        objMan->removeObject( m_plotCurves[curveIndex]->getId());
        m_plotCurves.removeAt( curveIndex );
        m_plotManager->removeData( name );
        _saveCurveState();
        _updateZoomRangeBasedOnPercent();
        _updatePlotData();
    }
    else {
        result = "Could not find profile curve "+name+" to remove.";
    }
    return result;
}


void Profiler::_profileRendered(const Carta::Lib::Hooks::ProfileResult& result,
        int curveIndex, const QString& layerName, bool createNew,
        std::shared_ptr<Carta::Lib::Image::ImageInterface> image){
    QString errorMessage = result.getError();
    if ( !errorMessage.isEmpty() ){
        ErrorManager* hr = Util::findSingletonObject<ErrorManager>();
        hr->registerError( errorMessage );
    }
    else {
        std::vector< std::pair<double,double> > data = result.getData();
        int dataCount = data.size();
        if ( dataCount > 0 ){
            std::vector<double> plotDataX( dataCount );
            std::vector<double> plotDataY( dataCount );

            for( int i = 0 ; i < dataCount; i ++ ){
                plotDataX[i] = data[i].first;
                plotDataY[i] = data[i].second;
            }

            std::shared_ptr<CurveData> profileCurve( nullptr );
            if ( curveIndex < 0 || createNew ){
                Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
                profileCurve.reset( objMan->createObject<CurveData>() );
                profileCurve->setImageName( layerName );
                double restFrequency = result.getRestFrequency();
                int significantDigits = m_state.getValue<int>( Util::SIGNIFICANT_DIGITS );
                double restRounded = Util::roundToDigits( restFrequency, significantDigits );
                QString restUnit = result.getRestUnits();
                profileCurve->setRestQuantity( restRounded, restUnit );
                _assignCurveName( profileCurve );
                _assignColor( profileCurve );
                m_plotCurves.append( profileCurve );
                profileCurve->setSource( image );

            }
            else {
                profileCurve = m_plotCurves[curveIndex];
            }

            profileCurve->setData( plotDataX, plotDataY );
            _saveCurveState();
            _generateFit();
            _updateZoomRangeBasedOnPercent();
            _updatePlotBounds();
            _updatePlotData();
        }
    }
}


QString Profiler::removeLink( CartaObject* cartaObject){
    bool removed = false;
    QString result;
    Controller* controller = dynamic_cast<Controller*>( cartaObject );
    if ( controller != nullptr ){
        removed = m_linkImpl->removeLink( controller );
        if ( removed ){
            controller->disconnect(this);
            m_controllerLinked = false;
            _clearData();
        }
    }
    else {
       result = "Profiler was unable to remove link only image links are supported";
    }
    return result;
}

void Profiler::_resetFitGuessPixels(){
    //Update the corresponding pixels.
    QString guessKey = Carta::State::UtilState::getLookup( CurveData::FIT, INITIAL_GUESSES );
    int guessCount = m_stateFit.getArraySize( guessKey );
    for ( int i = 0; i < guessCount; i++ ){
        QString indexKey = Carta::State::UtilState::getLookup( guessKey, i );
        QString centerKey = Carta::State::UtilState::getLookup( indexKey, FIT_CENTER );
        double center = m_stateFit.getValue<double>( centerKey );
        QString peakKey = Carta::State::UtilState::getLookup( indexKey, FIT_PEAK );
        double peak = m_stateFit.getValue<double>( peakKey );
        QString fbhwKey = Carta::State::UtilState::getLookup( indexKey, FIT_FBHW );
        double fbhw = m_stateFit.getValue<double>( fbhwKey );
        bool validCenter = false;
        QPointF centerPt = m_plotManager->getScreenPoint( QPointF(center,peak),&validCenter);
        bool validOffset = false;
        QPointF offsetPt = m_plotManager->getScreenPoint( QPointF(center - fbhw,peak), &validOffset );
        if ( validCenter && validOffset ){
            centerKey = Carta::State::UtilState::getLookup( indexKey, FIT_CENTER_PIXEL );
            m_stateFit.setValue<int>( centerKey, (int)(centerPt.x()) );
            peakKey = Carta::State::UtilState::getLookup( indexKey, FIT_PEAK_PIXEL );
            m_stateFit.setValue<int>( peakKey, (int)(centerPt.y()) );
            fbhwKey = Carta::State::UtilState::getLookup( indexKey, FIT_FBHW_PIXEL );
            m_stateFit.setValue<int>( fbhwKey, (int)(centerPt.x() - offsetPt.x()) );
        }
    }
    m_stateFit.flushState();
}

QString Profiler::resetRestFrequency( const QString& curveName ){
    QString result;
    int index = _findCurveIndex( curveName );
    if ( index >= 0 ){
        m_plotCurves[index]->resetRestFrequency();
        _saveCurveState( index );
        m_stateData.flushState();
        _generateData( m_plotCurves[index]->getImage(),
                    index, m_plotCurves[index]->getNameImage(), false );
    }
    else {
        result = "Could not reset rest frequency, unrecognized profile curve:"+curveName;
    }
    return result;
}

void Profiler::resetState( const QString& state ){
    StateInterface restoredState( "");
    restoredState.setState( state );
    QString settingStr = restoredState.getValue<QString>(Settings::SETTINGS);
    m_preferences->resetStateString( settingStr );
    QString prefStr = restoredState.getValue<QString>(Util::PREFERENCES);
    m_state.setState( prefStr );
    m_state.flushState();

    m_stateFit.setState( restoredState.getValue<QString>( CurveData::FIT) );
    m_stateFit.flushState();

    bool showMeanRMS = m_state.getValue<bool>( SHOW_MEAN_RMS );
    m_plotManager->setHLineVisible( showMeanRMS );
    m_plotManager->setRangeMarkerVisible( showMeanRMS );

}


void Profiler::_saveCurveState( int index ){
    QString key = Carta::State::UtilState::getLookup( CURVES, index );
    QString curveState = m_plotCurves[index]->getStateString();
    m_stateData.setObject( key, curveState );
}

void Profiler::_saveCurveState(){
    int curveCount = m_plotCurves.size();
    m_stateData.resizeArray( CURVES, curveCount );
    for ( int i = 0; i < curveCount; i++ ){
       _saveCurveState( i );
    }
    m_stateData.flushState();
}

QString Profiler::setAxisUnitsBottom( const QString& unitStr ){
    QString result;
    QString actualUnits = m_spectralUnits->getActualUnits( unitStr );
    if ( !actualUnits.isEmpty() ){
        QString oldBottomUnits = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
        if ( actualUnits != oldBottomUnits ){
            //Change the units in the curves.
            int curveCount = m_plotCurves.size();
            for ( int i = 0; i < curveCount; i++ ){
                std::vector<double> converted = _convertUnitsX( m_plotCurves[i], actualUnits );
                m_plotCurves[i]->setDataX( converted );
            }

            //Update the state & graph
            m_state.setValue<QString>( AXIS_UNITS_BOTTOM, actualUnits);
            m_plotManager->setTitleAxisX( _getUnitType( actualUnits ) );
            m_state.flushState();

            //Set the zoom min & max based on new units
            _updateZoomRangeBasedOnPercent();

            //Tell the plot about the new bounds.
            _updatePlotBounds();

            //Put the data into the plot
            _updatePlotData();

            //Update channel line
            _updateChannel( _getControllerSelected(), Carta::Lib::AxisInfo::KnownType::SPECTRAL );
        }
    }
    else {
        result = "Unrecognized profile bottom axis units: "+unitStr;
    }
    return result;
}

QString Profiler::setAxisUnitsLeft( const QString& unitStr ){
    QString result;
    QString actualUnits = m_intensityUnits->getActualUnits( unitStr );
    if ( !actualUnits.isEmpty() ){
        QString oldLeftUnits = m_state.getValue<QString>( AXIS_UNITS_LEFT );
        if ( oldLeftUnits != actualUnits ){
            //Convert the units in the curves.
            int curveCount = m_plotCurves.size();
            for ( int i = 0; i < curveCount; i++ ){
                std::vector<double> converted = _convertUnitsY( m_plotCurves[i], actualUnits );
                m_plotCurves[i]->setDataY( converted );
            }
            //Update the state and plot
            m_state.setValue<QString>( AXIS_UNITS_LEFT, actualUnits );
            m_state.flushState();
            _updatePlotData();
            m_plotManager->setTitleAxisY( actualUnits );
        }
    }
    else {
        result = "Unrecognized profile left axis units: "+unitStr;
    }
    return result;
}


QStringList Profiler::setCurveColor( const QString& name, int redAmount, int greenAmount, int blueAmount ){
    QStringList result;
    const int MAX_COLOR = 255;
    bool validColor = true;
    if ( redAmount < 0 || redAmount > MAX_COLOR ){
        validColor = false;
        result.append("Profile curve red amount must be in [0,"+QString::number(MAX_COLOR)+"]: "+QString::number(redAmount) );
    }
    if ( greenAmount < 0 || greenAmount > MAX_COLOR ){
        validColor = false;
        result.append("Profile curve green amount must be in [0,"+QString::number(MAX_COLOR)+"]: "+QString::number(greenAmount) );
    }
    if ( blueAmount < 0 || blueAmount > MAX_COLOR ){
        validColor = false;
        result.append("Profile curve blue amount must be in [0,"+QString::number(MAX_COLOR)+"]: "+QString::number(blueAmount) );
    }
    if ( validColor ){
        int index = _findCurveIndex( name );
        if ( index >= 0 ){
            QColor oldColor = m_plotCurves[index]->getColor();
            QColor curveColor( redAmount, greenAmount, blueAmount );
            if ( oldColor.name() != curveColor.name() ){
                m_plotCurves[index]->setColor( curveColor );
                _saveCurveState( index );
                m_stateData.flushState();
                m_plotManager->setColor( curveColor, name );
            }
        }
        else {
            result.append( "Unrecognized profile curve:"+name );
        }
    }
    return result;
}


QString Profiler::setCurveName( const QString& id, const QString& newName ){
    QString result;
    int curveIndex = _findCurveIndex( id );
    if ( curveIndex >= 0 ){
        result = m_plotCurves[curveIndex]->setName( newName );
        _saveCurveState( curveIndex );
        m_stateData.flushState();
        m_plotManager->setCurveName( id, newName );
    }
    else {
        result = "Profile name could not be set because of invalid identifier: "+id;
    }
    return result;
}

void Profiler::_setErrorMargin(){
    int significantDigits = m_state.getValue<int>(Util::SIGNIFICANT_DIGITS );
    m_errorMargin = 1.0/qPow(10,significantDigits);
}

QString Profiler::setFitCurves( const QStringList curveNames ){
    QString result;
    int fitCount = curveNames.size();
    //Clear any that were previously selected for fitting.
    int curveCount = m_plotCurves.size();
    bool changed = false;
    for ( int i = 0; i < curveCount; i++ ){
        if ( m_plotCurves[i]->isSelectedFit()){
            m_plotCurves[i]->setSelectedFit( false );
            changed = true;
        }
    }

    //Set the ones identified selected.
    bool updatedFitState = false;
    for ( int i = 0; i < fitCount; i++ ){
        int curveIndex = _findCurveIndex( curveNames[i]);
        if ( curveIndex >= 0 ){
            changed = true;
            m_plotCurves[curveIndex]->setSelectedFit( true );
            //Update our state with the fit state of the first curve that was selected.
            if ( !updatedFitState ){
                m_stateFit.setObject( CurveData::FIT, m_plotCurves[curveIndex]->getFitParams());
                m_stateFit.flushState();
                updatedFitState = true;
            }
        }
        else {
            if ( result.isEmpty() ){
                result = "Could not fit selected curve(s); invalid identifier(s):";
            }
            result = result + curveNames[i];
        }
    }

    if ( changed ){
        _saveCurveState();
    }
    return result;
}

void Profiler::setFitManualGuess( bool manualGuess ){
    QString key = Carta::State::UtilState::getLookup( CurveData::FIT, MANUAL_GUESS );
    bool oldManual = m_stateFit.getValue<bool>( key );
    if ( oldManual != manualGuess ){
        m_stateFit.setValue<bool>( key, manualGuess );
        m_stateFit.flushState();
        _generateFit();
    }
}

void Profiler::_makeInitialGuesses( int count ){
    QString guessKey = Carta::State::UtilState::getLookup( CurveData::FIT, INITIAL_GUESSES );
    int oldCount = m_stateFit.getArraySize( guessKey );
    int diffCount = count - oldCount;
    if ( diffCount != 0 ){
        //Update the initial guess count in the curves.
        m_stateFit.resizeArray( guessKey, count, Carta::State::StateInterface::PreserveAll );
    }
    if ( diffCount > 0 ){
        //Set up uniformly spaced guesses based on the first curve that has been
        //selected to fit.
        bool random = true;
        if ( oldCount == 0 ){
            random = false;
        }

        std::vector<std::tuple<double,double,double> > guesses = _generateFitGuesses( diffCount, random );
        //Store the guesses.
        for ( int i = oldCount; i < count; i++ ){
            QString indexKey = Carta::State::UtilState::getLookup( guessKey, i );
            m_stateFit.setObject( indexKey );
            QString centerKey = Carta::State::UtilState::getLookup( indexKey, FIT_CENTER );
            m_stateFit.insertValue<double>( centerKey, std::get<0>(guesses[i-oldCount]) );
            QString peakKey = Carta::State::UtilState::getLookup( indexKey, FIT_PEAK );
            m_stateFit.insertValue<double>( peakKey, std::get<1>(guesses[i-oldCount]) );
            QString fbhwKey = Carta::State::UtilState::getLookup( indexKey, FIT_FBHW );
            m_stateFit.insertValue<double>( fbhwKey, std::get<2>(guesses[i-oldCount]) );

            //Pixels
            centerKey = Carta::State::UtilState::getLookup( indexKey, FIT_CENTER_PIXEL );
            m_stateFit.insertValue<int>( centerKey, 0 );
            peakKey = Carta::State::UtilState::getLookup( indexKey, FIT_PEAK_PIXEL );
            m_stateFit.insertValue<int>( peakKey, 0 );
            fbhwKey = Carta::State::UtilState::getLookup( indexKey, FIT_FBHW_PIXEL );
            m_stateFit.insertValue<int>( fbhwKey, 1 );
        }
    }
}

QString Profiler::setFitInitialGuessesPixels(const std::vector<std::tuple<int,int,int> >& guessPixels ){
    QString result;
    int guessCount = guessPixels.size();
    QString baseKey = Carta::State::UtilState::getLookup( CurveData::FIT, INITIAL_GUESSES );
    for ( int i = 0; i < guessCount; i++ ){
        int centerPixel = std::get<0>( guessPixels[i] );
        int peakPixel = std::get<1>( guessPixels[i] );
        int fbhwPixel = std::get<2>( guessPixels[i] );
        if ( centerPixel < 0 || peakPixel < 0 || fbhwPixel < 0 ){
            QString coord( "("+QString::number(centerPixel)+","+QString::number(peakPixel)+","+
                    QString::number(fbhwPixel)+")");
            result = "Pixel coordinates of initial guesses must be nonnegative integers: " + coord;
            break;
        }
        //Set the pixel coordinates if they have changed.
        QString indexKey = Carta::State::UtilState::getLookup( baseKey, i );
        bool centerChanged = false;
        bool peakChanged = false;
        bool fbhwChanged = false;
        QString centerKey = Carta::State::UtilState::getLookup( indexKey, FIT_CENTER_PIXEL );
        int oldCenter = m_stateFit.getValue<int>( centerKey );
        if ( oldCenter != centerPixel ){
            m_stateFit.setValue<int>( centerKey, centerPixel );
            centerChanged = true;
        }
        QString peakKey = Carta::State::UtilState::getLookup( indexKey, FIT_PEAK_PIXEL );
        int oldPeak = m_stateFit.getValue<int>( peakKey );
        if ( oldPeak != peakPixel ){
            m_stateFit.setValue<int>( peakKey, peakPixel );
            peakChanged = true;
        }
        QString fbhwKey = Carta::State::UtilState::getLookup( indexKey, FIT_FBHW_PIXEL );
        int oldFBHW = m_stateFit.getValue<int>( fbhwKey );
        if ( oldFBHW != fbhwPixel ){
            m_stateFit.setValue<int>( fbhwKey, fbhwPixel );
            fbhwChanged = true;
        }

        //We recalculate the image image coordinates based on the pixel coordinates.
        bool generate = false;
        QPointF centerPt = m_plotManager->getImagePoint( QPointF(centerPixel,peakPixel) );
        if ( centerChanged || peakChanged ){
            centerKey = Carta::State::UtilState::getLookup( indexKey, FIT_CENTER );
            if ( qAbs(m_stateFit.getValue<double>(centerKey) - centerPt.x()) > ERROR_MARGIN ){
                m_stateFit.setValue<double>( centerKey, centerPt.x());
                generate = true;
            }
            peakKey = Carta::State::UtilState::getLookup( indexKey, FIT_PEAK );
            if ( qAbs(m_stateFit.getValue<double>(peakKey) - centerPt.y()) > ERROR_MARGIN ){
                m_stateFit.setValue<double>( peakKey, centerPt.y());
                generate = true;
            }
        }
        if ( fbhwChanged ){
            QPointF offsetPt = m_plotManager->getImagePoint( QPointF(centerPixel - fbhwPixel, peakPixel) );
            fbhwKey = Carta::State::UtilState::getLookup( indexKey, FIT_FBHW );
            double fbhwNew = centerPt.x() - offsetPt.x();
            if ( qAbs( m_stateFit.getValue<double>( fbhwKey) - fbhwNew) > ERROR_MARGIN ){
                m_stateFit.setValue<double>( fbhwKey, fbhwNew );
                generate = true;
            }
        }
        if ( centerChanged || peakChanged || fbhwChanged ){
            m_stateFit.flushState();
        }
        if ( generate ){
            _generateFit();
        }
    }
    return result;
}

QString Profiler::setFitInitialGuesses(const std::vector<std::tuple<double,double,double> >& guesses ){
    QString result;
    int guessCount = guesses.size();
    QString baseKey = Carta::State::UtilState::getLookup( CurveData::FIT, INITIAL_GUESSES );
    int storedArraySize = m_stateFit.getArraySize( baseKey );
    if ( guessCount != storedArraySize ){
        result = "There must be exactly "+QString::number(storedArraySize)+" initial fit guesses.";
    }
    else {
        bool changed = false;
        for ( int i = 0; i < guessCount; i++ ){
            QString indexKey = Carta::State::UtilState::getLookup( baseKey, i );
            QString centerKey = Carta::State::UtilState::getLookup( indexKey, FIT_CENTER );
            double oldCenter = m_stateFit.getValue<double>( centerKey );
            double center = std::get<0>( guesses[i] );
            if ( qAbs( oldCenter - center ) > ERROR_MARGIN ){
                m_stateFit.setValue<double>( centerKey, center );
                changed = true;
            }
            QString peakKey = Carta::State::UtilState::getLookup( indexKey, FIT_PEAK );
            double oldPeak = m_stateFit.getValue<double>( peakKey );
            double peak = std::get<1>( guesses[i] );
            if ( qAbs( oldPeak - peak ) > ERROR_MARGIN ){
                m_stateFit.setValue<double>( peakKey, peak );
                changed = true;
            }
            QString fbhwKey = Carta::State::UtilState::getLookup( indexKey, FIT_FBHW );
            double oldFBHW = m_stateFit.getValue<double>( fbhwKey );
            double fbhw = std::get<2>(guesses[i]);
            if ( qAbs( oldFBHW - fbhw ) > ERROR_MARGIN ){
                m_stateFit.setValue<double>( fbhwKey, fbhw );
                changed = true;
            }
        }
        if ( changed ){
            //Update the pixel coordinates.
            _resetFitGuessPixels();
            _generateFit();
        }
    }
    return result;
}


void Profiler::resetInitialFitGuesses(){
    QString baseKey = Carta::State::UtilState::getLookup( CurveData::FIT, INITIAL_GUESSES );
    int arraySize = m_stateFit.getArraySize( baseKey );
    std::vector<std::tuple<double,double,double> > guesses = _generateFitGuesses( arraySize, false );
    setFitInitialGuesses( guesses );
}

QString Profiler::setGaussCount( int count ){
    QString result;
    if ( count >= 0 ){
        QString key = Carta::State::UtilState::getLookup( CurveData::FIT, GAUSS_COUNT );
        int oldCount = m_stateFit.getValue<int>( key );
        if ( count != oldCount ){
            m_stateFit.setValue<int>( key, count );

            if ( m_plotCurves.size() > 0 ){
                //Update the initial guess count in the curves.
                _makeInitialGuesses( count );
                //Reset the pixel estimates based on plot size
                _resetFitGuessPixels();
            }
            else {
                //No initial gaussian guesses if there are no curves to fit.
                QString guessKey = Carta::State::UtilState::getLookup( CurveData::FIT, INITIAL_GUESSES );
                m_stateFit.resizeArray( guessKey, 0 );
            }
            m_stateFit.flushState();
            _generateFit();
        }
    }
    else {
        result = "Profile fit Gaussian count must be nonnegative: "+QString::number( count);
    }
    return result;
}




QString Profiler::setGenerateMode( const QString& modeStr ){
    QString result;
    QString actualMode = m_generateModes->getActualMode( modeStr );
    if ( !actualMode.isEmpty() ){
        QString oldMode = m_state.getValue<QString>( GEN_MODE );
        if ( actualMode != oldMode ){
            m_state.setValue<QString>( GEN_MODE, actualMode);
            m_state.flushState();
            Controller* controller = _getControllerSelected();
            _loadProfile( controller );
        }
    }
    else {
        result = "Unrecognized profile generation mode: "+modeStr;
    }
    return result;
}

void Profiler::setGridLines( bool showLines ){
    bool oldShowLines = m_state.getValue<bool>( GRID_LINES );
    if ( oldShowLines != showLines ){
        m_state.setValue<bool>( GRID_LINES, showLines );
        m_state.flushState();
        m_plotManager->setGridLines( showLines );
    }
}


QString Profiler::setLineStyle( const QString& name, const QString& lineStyle ){
    QString result;
    int index = _findCurveIndex( name );
    if ( index >= 0 ){
        result = m_plotCurves[index]->setLineStyle( lineStyle );
        if ( result.isEmpty() ){
            _saveCurveState( index );
            m_stateData.flushState();
            LineStyles* lineStyles = Util::findSingletonObject<LineStyles>();
            QString actualStyle = lineStyles->getActualLineStyle( lineStyle );
            m_plotManager->setLineStyle( actualStyle, name );
        }
    }
    else {
        result = "Profile curve was not recognized: "+name;
    }
    return result;
}

QString Profiler::setLegendLocation( const QString& locateStr ){
    QString result;
    QString actualLocation = m_legendLocations->getActualLocation( locateStr );
    if ( !actualLocation.isEmpty() ){
        QString oldLocation = m_state.getValue<QString>( LEGEND_LOCATION );
        if ( oldLocation != actualLocation ){
            m_state.setValue<QString>( LEGEND_LOCATION, actualLocation );
            m_state.flushState();
            m_plotManager->setLegendLocation( actualLocation );
        }
    }
    else {
        result = "Unrecognized profile legend location: "+locateStr;
    }
    return result;
}

void Profiler::setLegendExternal( bool external ){
    bool oldExternal = m_state.getValue<bool>( LEGEND_EXTERNAL );
    if ( external != oldExternal ){
        m_state.setValue<bool>( LEGEND_EXTERNAL, external );
        m_legendLocations->setAvailableLocations(external);
        //Check to see if the current location is still supported.  If not,
        //use the default.
        QString currPos = m_state.getValue<QString>( LEGEND_LOCATION );
        QString actualPos = m_legendLocations->getActualLocation( currPos );
        if ( actualPos.isEmpty() ){
            QString newPos = m_legendLocations->getDefaultLocation( external );
            m_state.setValue<QString>( LEGEND_LOCATION, newPos );
        }
        m_state.flushState();

        m_plotManager->setLegendExternal( external );
    }
}

void Profiler::setLegendLine( bool showLegendLine ){
    bool oldShowLine = m_state.getValue<bool>( LEGEND_LINE );
    if ( oldShowLine != showLegendLine ){
        m_state.setValue<bool>(LEGEND_LINE, showLegendLine );
        m_state.flushState();
        m_plotManager->setLegendLine( showLegendLine );
    }
}

void Profiler::setLegendShow( bool showLegend ){
    bool oldShowLegend = m_state.getValue<bool>( LEGEND_SHOW );
    if ( oldShowLegend != showLegend ){
        m_state.setValue<bool>(LEGEND_SHOW, showLegend );
        m_state.flushState();
        m_plotManager->setLegendShow( showLegend );
    }
}

QString Profiler::setPlotStyle( const QString& name, const QString& plotStyle ){
    QString result;
    int index = _findCurveIndex( name );
    if ( index >= 0 ){
        result = m_plotCurves[index]->setPlotStyle( plotStyle );
        if ( result.isEmpty() ){
            _saveCurveState( index );
            m_stateData.flushState();
            ProfilePlotStyles* plotStyles = Util::findSingletonObject<ProfilePlotStyles>();
            QString actualStyle = plotStyles->getActualStyle( plotStyle );
            m_plotManager->setStyle( actualStyle, name );
        }
    }
    else {
        result = "Profile curve was not recognized: "+name;
    }
    return result;
}

QString Profiler::setPolyCount( int count ){
    QString result;
    if ( count >= 0 ){
        QString key = Carta::State::UtilState::getLookup( CurveData::FIT, POLY_DEGREE );
        int oldCount = m_stateFit.getValue<int>( key );
        if ( count != oldCount ){
            m_stateFit.setValue<int>( key, count );
            m_stateFit.flushState();
            _generateFit();
        }
    }
    else {
        result = "Profile fit polynomial count must be nonnegative: "+QString::number( count);
    }
    return result;
}

void Profiler::setRandomHeuristics( bool randomHeuristics ){
    QString key = Carta::State::UtilState::getLookup( CurveData::FIT, HEURISTICS );
    bool oldRandom = m_stateFit.getValue<bool>( key );
    if ( randomHeuristics != oldRandom ){
        m_stateFit.setValue<bool>( key, randomHeuristics );
        m_stateFit.flushState();
        _generateFit();
    }
}

QString Profiler::setRestFrequency( double freq, const QString& curveName ){
    QString result;
    int index = _findCurveIndex( curveName );
    if ( index >= 0 ){
        int significantDigits = m_state.getValue<int>( Util::SIGNIFICANT_DIGITS );
        double roundedFreq = Util::roundToDigits( freq, significantDigits );
        bool freqSet = false;
        result = m_plotCurves[index]->setRestFrequency( roundedFreq, m_errorMargin, &freqSet );
        if ( freqSet ){
            _saveCurveState( index );
            m_stateData.flushState();
            _generateData( m_plotCurves[index]->getImage(),
                    index, m_plotCurves[index]->getNameImage(), false );
        }
    }
    else {
        result = "Unrecognized profile curve: "+curveName;
    }
    return result;
}

QString Profiler::setRestUnits( const QString& restUnits, const QString& curveName ){
    QString result;
    int index = _findCurveIndex( curveName );
    if ( index >= 0 ){
        int signDigits = m_state.getValue<int>( Util::SIGNIFICANT_DIGITS );
        result = m_plotCurves[index]->setRestUnits( restUnits, signDigits, m_errorMargin );
        if ( result.isEmpty() ){
            _saveCurveState( index );
            m_stateData.flushState();
        }
    }
    else {
        result = "Unrecognized profile curve: "+curveName;
    }
    return result;
}


QString Profiler::setRestUnitType( bool restUnitsFreq, const QString& curveName ){
    QString result;
    int index = _findCurveIndex( curveName );
    if ( index >= 0 ){
        int signDigits = m_state.getValue<int>( Util::SIGNIFICANT_DIGITS );
        m_plotCurves[index]->setRestUnitType( restUnitsFreq, signDigits, m_errorMargin );
        _saveCurveState( index );
        m_stateData.flushState();
    }
    else {
        result = "Unrecognized profile curve:"+curveName;
    }
    return result;
}

QString Profiler::setShowFitGuesses( bool showFitGuesses ){
    QString result;
    bool oldShow = m_state.getValue<bool>( SHOW_GUESSES );
    if ( oldShow != showFitGuesses ){
        //We will only show them if we are in manual mode.
        if ( isFitManualGuess() ){
            m_state.setValue<bool>(SHOW_GUESSES, showFitGuesses );
            m_state.flushState();
        }
        else {
            result = "Fit guesses will only be displayed when manual guesses are specified.";
        }
    }
    return result;
}

void Profiler::setShowFitResiduals( bool showFitResiduals ){
    bool oldShowResiduals = m_state.getValue<bool>( SHOW_RESIDUALS );
    if ( oldShowResiduals != showFitResiduals ){
        m_state.setValue<bool>(SHOW_RESIDUALS, showFitResiduals );
        m_state.flushState();
    }
}

void Profiler::setShowFitStatistics( bool showFitStatistics ){
    bool oldShow = m_state.getValue<bool>( SHOW_STATISTICS );
    if ( oldShow != showFitStatistics ){
        m_state.setValue<bool>(SHOW_STATISTICS, showFitStatistics );
        m_state.flushState();
    }
}

void Profiler::setShowMeanRMS( bool showMeanRMS ){
    bool oldShow = m_state.getValue<bool>( SHOW_MEAN_RMS );
    if ( oldShow != showMeanRMS ){
        m_state.setValue<bool>(SHOW_MEAN_RMS, showMeanRMS );
        m_state.flushState();
        m_plotManager->setRangeMarkerVisible( showMeanRMS );
        m_plotManager->setHLineVisible( showMeanRMS );
        m_plotManager->updatePlot();
    }
}

void Profiler::setShowPeakLabels( bool showPeakLabels ){
    bool oldShow = m_state.getValue<bool>( SHOW_PEAK_LABELS );
    if ( oldShow != showPeakLabels ){
        m_state.setValue<bool>(SHOW_PEAK_LABELS, showPeakLabels );
        m_state.flushState();
    }
}

QString Profiler::setSignificantDigits( int digits ){
    QString result;
    if ( digits <= 0 ){
        result = "Invalid significant digits; must be positive:  "+QString::number( digits );
    }
    else {
        if ( m_state.getValue<int>(Util::SIGNIFICANT_DIGITS) != digits ){
            m_state.setValue<int>(Util::SIGNIFICANT_DIGITS, digits );
            _setErrorMargin();
        }
    }
    return result;
}

QString Profiler::setStatistic( const QString& statStr, const QString& curveName ){
    QString result;
    int index = _findCurveIndex( curveName );
    if ( index >= 0 ){
        result = m_plotCurves[index]->setStatistic( statStr );
        if ( result.isEmpty() ){
            _saveCurveState( index );
            m_stateData.flushState();
            Carta::Lib::ProfileInfo::AggregateType agType = m_stats->getTypeFor( statStr );
            m_intensityUnits->resetUnits( agType );
            QString unitDefault = m_intensityUnits->getDefault();
            setAxisUnitsLeft( unitDefault );
            _generateData( m_plotCurves[index]->getImage(),
                               index, m_plotCurves[index]->getNameImage(), false );
        }
    }
    else {
        result = "Could not set the profile statistic - unrecognized curve: "+curveName;
    }
    return result;
}

QString Profiler::setTabIndex( int index ){
    QString result;
    if ( index >= 0 ){
        int oldIndex = m_state.getValue<int>( Util::TAB_INDEX );
        if ( index != oldIndex ){
            m_state.setValue<int>( Util::TAB_INDEX, index );
            m_state.flushState();
        }
    }
    else {
        result = "Profile tab index must be nonnegative: "+ QString::number(index);
    }
    return result;
}


void Profiler::setZoomBuffer( bool zoomBuffer ){
    bool oldZoomBuffer = m_stateData.getValue<bool>( ZOOM_BUFFER );
    if ( oldZoomBuffer != zoomBuffer ){
        m_stateData.setValue<bool>( ZOOM_BUFFER, zoomBuffer );
        m_stateData.flushState();
        _updatePlotBounds();
    }

}

QString Profiler::setZoomBufferSize( double zoomBufferSize ){
    QString result;
    if ( zoomBufferSize >= 0 && zoomBufferSize < 100 ){
        double oldBufferSize = m_stateData.getValue<double>( ZOOM_BUFFER_SIZE );
        int significantDigits = m_state.getValue<int>( Util::SIGNIFICANT_DIGITS );
        double roundedSize = Util::roundToDigits( zoomBufferSize, significantDigits );
        if ( qAbs( roundedSize - oldBufferSize) > m_errorMargin ){
            m_stateData.setValue<double>( ZOOM_BUFFER_SIZE, roundedSize );
            m_stateData.flushState();
            _updatePlotBounds();
        }
    }
    else {
        result = "Zoom buffer size must be in [0,100): "+QString::number(zoomBufferSize);
    }
    return result;
}


QString Profiler::setZoomRange( double zoomMin, double zoomMax ){
    QString result;
    double significantDigits = m_state.getValue<int>( Util::SIGNIFICANT_DIGITS );
    double zoomMinRounded = Util::roundToDigits( zoomMin, significantDigits );
    double zoomMaxRounded = Util::roundToDigits( zoomMax, significantDigits );
    if ( zoomMinRounded < zoomMaxRounded ){
        bool changed = false;
        double oldZoomMin = m_stateData.getValue<double>( ZOOM_MIN );
        if ( qAbs( zoomMinRounded - oldZoomMin ) > m_errorMargin ){
            changed = true;
            m_stateData.setValue<double>( ZOOM_MIN, zoomMinRounded );
        }
        double oldZoomMax = m_stateData.getValue<double>( ZOOM_MAX );
        if ( qAbs( zoomMaxRounded - oldZoomMax ) > m_errorMargin ){
            changed = true;
            m_stateData.setValue<double>( ZOOM_MAX, zoomMaxRounded );
        }
        if ( changed ){
            //Update the percents to match.
            std::pair<double,double> curveRange = _getCurveRangeX();

            double lowerPercent = 0;
            double upperPercent = 100;
            double curveSpan = curveRange.second - curveRange.first;
            if ( curveSpan > 0 ){
                if ( curveRange.first < zoomMinRounded ){
                    double diff = zoomMinRounded - curveRange.first;
                    lowerPercent = diff / curveSpan * 100;
                    lowerPercent = Util::roundToDigits( lowerPercent, significantDigits );
                }
                if ( curveRange.second > zoomMaxRounded ){
                    double diff = curveRange.second - zoomMaxRounded;
                    upperPercent = 100 - diff / curveSpan * 100;
                    upperPercent = Util::roundToDigits( upperPercent, significantDigits );
                }
            }
            m_stateData.setValue<double>( ZOOM_MIN_PERCENT, lowerPercent );
            m_stateData.setValue<double>( ZOOM_MAX_PERCENT, upperPercent );
            m_stateData.flushState();

           _updatePlotBounds();
        }
    }
    else {
        result = "Minimum zoom, "+QString::number(zoomMin)+", must be less the maximum zoom, "+QString::number(zoomMax);
    }
    return result;
}


QString Profiler::setZoomRangePercent( double zoomMinPercent, double zoomMaxPercent ){
    QString result;

    if ( 0 <= zoomMinPercent && zoomMinPercent <= 100 ){
        if ( 0 <= zoomMaxPercent && zoomMaxPercent <= 100 ){
            int significantDigits = m_state.getValue<int>( Util::SIGNIFICANT_DIGITS );
            double zoomMinPercentRounded = Util::roundToDigits( zoomMinPercent, significantDigits );
            double zoomMaxPercentRounded = Util::roundToDigits( zoomMaxPercent, significantDigits );
            if ( zoomMinPercentRounded < zoomMaxPercentRounded ){
                bool changed = false;
                double oldZoomMinPercent = m_stateData.getValue<double>( ZOOM_MIN_PERCENT );
                if ( qAbs( zoomMinPercentRounded - oldZoomMinPercent ) > m_errorMargin ){
                    changed = true;
                    m_stateData.setValue<double>( ZOOM_MIN_PERCENT, zoomMinPercentRounded );
                }
                double oldZoomMaxPercent = m_stateData.getValue<double>( ZOOM_MAX_PERCENT );
                if ( qAbs( zoomMaxPercentRounded - oldZoomMaxPercent ) > m_errorMargin ){
                    changed = true;
                    m_stateData.setValue<double>( ZOOM_MAX_PERCENT, zoomMaxPercentRounded );
                }
                if ( changed ){
                    //Update the zoom min and max.
                    _updateZoomRangeBasedOnPercent();
                    //Update the graph.
                    _updatePlotBounds();
                }
            }
            else {
                result = "Zoom minimum percent: "+ QString::number(zoomMinPercent)+" must be less than "+QString::number( zoomMaxPercent);
            }
        }
        else {
            result = "Invalid zoom right percent [0,100]: "+QString::number(zoomMaxPercent);
        }
    }
    else {
        result = "Invalid zoom left percent [0,100]: "+QString::number( zoomMinPercent);
    }
    return result;
}



void Profiler::timerEvent( QTimerEvent* /*event*/ ){
    Controller* controller = _getControllerSelected();
    if ( controller ){
        controller->_setFrameAxis( m_oldFrame, Carta::Lib::AxisInfo::KnownType::SPECTRAL );
        _updateChannel( controller, Carta::Lib::AxisInfo::KnownType::SPECTRAL );
        if ( m_oldFrame < m_currentFrame ){
            m_oldFrame++;
        }
        else if ( m_oldFrame > m_currentFrame ){
            m_oldFrame--;
        }
        else {
            killTimer(m_timerId );
            m_timerId = 0;
        }
    }
}


void Profiler::_updateAvailableImages( Controller* controller ){
    std::vector<std::shared_ptr<Layer> > dataSources = controller->getLayers();
    int dCount = dataSources.size();
    m_stateData.resizeArray( IMAGES, dCount );
    for ( int i = 0; i < dCount; i++ ){
        QString layerName = dataSources[i]->_getLayerName();
        QString lookup = Carta::State::UtilState::getLookup( IMAGES, i );
        m_stateData.setValue( lookup, layerName );
    }
    m_stateData.flushState();
}


void Profiler::_updateChannel( Controller* controller, Carta::Lib::AxisInfo::KnownType type ){
    if ( type == Carta::Lib::AxisInfo::KnownType::SPECTRAL ){
        int frame = controller->getFrame( type );
        //Convert the frame to the units the plot is using.
        QString bottomUnits = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
        QString units = _getUnitUnits( bottomUnits );
        std::vector<double> values(1);
        values[0] = frame;
        if ( m_plotCurves.size() > 0 ){
            if ( !units.isEmpty() ){
                std::shared_ptr<Carta::Lib::Image::ImageInterface> imageSource = m_plotCurves[0]->getSource();
                _convertX(  values, imageSource, "", units );
            }
            m_plotManager->setVLinePosition( values[0] );
        }
    }
}



void Profiler::_updateFitStatistics( const std::vector<Carta::Lib::Hooks::FitResult>& results ){
    int resultCount = results.size();
    QString stats;
    for ( int i = 0; i < resultCount; i++ ){
        stats = stats + _updateFitStatistic( i, results[i]);
    }
    m_stateFitStatistics.setValue<QString>( FIT_STATISTICS, stats );
    m_stateFitStatistics.flushState();
}

QString Profiler::_updateFitStatistic( int index, const Carta::Lib::Hooks::FitResult& result ){
    QStringList fitStats;

    fitStats.append("Fit "+QString::number(index+1)+": "+ _getFitStatusMessage(result.getStatus()));
    fitStats.append( "  RMS: "+QString::number(result.getRMS())+
            "  Diff Squares:"+QString::number(result.getDiffSquare())+"<br/>");
    std::vector<std::tuple<double,double,double> > gaussFits = result.getGaussFits();
    int gaussCount = gaussFits.size();
    for ( int i = 0; i < gaussCount; i++ ){
        fitStats.append( "&nbsp;&nbsp;&nbsp;&nbsp;Gaussian "+ QString::number(i+1)+
                ": Center("+QString::number(std::get<0>(gaussFits[i]))+
                "), Peak("+QString::number(std::get<1>(gaussFits[i])) +
                "), FWBH("+QString::number(std::get<2>(gaussFits[i]))+")<br/>");
    }
    std::vector<double> polyTerms = result.getPolyCoefficients();
    int termCount = polyTerms.size();
    if ( termCount > 0 ){
        QString polyTermsStr( "&nbsp;&nbsp;&nbsp;&nbsp;Polynomial Coefficients: ");
        for ( int i = 0; i < termCount; i++ ){
            polyTermsStr = polyTermsStr + QString::number(polyTerms[i]);
            if ( i < termCount - 1 ){
                polyTermsStr = polyTermsStr + ", ";
            }
        }
        fitStats.append( polyTermsStr );
    }
    return fitStats.join("");
}

void Profiler::_updatePlotBounds(){
    //Update the graph.
    //See if we need to add an additional buffer.
    double graphMin = m_stateData.getValue<double>( ZOOM_MIN );
    double graphMax = m_stateData.getValue<double>( ZOOM_MAX );
    double plotRange = graphMax - graphMin;
    if ( m_stateData.getValue<bool>( ZOOM_BUFFER) ){
        double bufferSize = m_stateData.getValue<double>( ZOOM_BUFFER_SIZE );
        double halfSize = bufferSize / 2;
        double buffAmount = plotRange * halfSize / 100;
        graphMin = graphMin - buffAmount;
        graphMax = graphMax + buffAmount;
    }
    m_plotManager->setAxisXRange( graphMin, graphMax );
}


void Profiler::_updateZoomRangeBasedOnPercent(){
    std::pair<double,double> range = _getCurveRangeX();
    double curveSpan = range.second - range.first;
    double minPercent = getZoomMinPercent();
    double maxPercent = getZoomMaxPercent();
    double zoomMin = range.first + minPercent* curveSpan / 100;
    double zoomMax = range.second -(100 - maxPercent)* curveSpan / 100;
    int significantDigits = m_state.getValue<int>( Util::SIGNIFICANT_DIGITS );
    zoomMin = Util::roundToDigits( zoomMin, significantDigits );
    zoomMax = Util::roundToDigits( zoomMax, significantDigits );
    double oldZoomMin = getZoomMin();
    double oldZoomMax = getZoomMax();
    bool changed = false;
    if ( qAbs( oldZoomMin - zoomMin ) > m_errorMargin ){
        m_stateData.setValue<double>( ZOOM_MIN, zoomMin );
        changed = true;
    }
    if ( qAbs( oldZoomMax - zoomMax ) > m_errorMargin ){
        m_stateData.setValue<double>( ZOOM_MAX, zoomMax );
        changed = true;
    }
    if ( changed ){
        m_stateData.flushState();
    }
}

void Profiler::_updatePlotData(){
    int curveCount = m_plotCurves.size();
    //Put the data into the plot.
    for ( int i = 0; i < curveCount; i++ ){
        std::vector< std::pair<double,double> > plotData = m_plotCurves[i]->getPlotData();
        QString dataId = m_plotCurves[i]->getName();
        Carta::Lib::Hooks::Plot2DResult plotResult( dataId, "", "", plotData );
        m_plotManager->addData( &plotResult );
        bool fitted = m_plotCurves[i]->isFitted();
        if ( fitted ){
            std::vector< std::pair<double,double> > fitData = m_plotCurves[i]->getFitData();
            Carta::Lib::Hooks::Plot2DResult fitResult( dataId+"Fit", "", "", fitData );

            m_plotManager->addData( &fitResult );
        }
        m_plotManager->setColor( m_plotCurves[i]->getColor(), dataId );
    }

    QString bottomUnit = m_state.getValue<QString>( AXIS_UNITS_BOTTOM );
    bottomUnit = _getUnitUnits( bottomUnit );
    QString leftUnit = m_state.getValue<QString>( AXIS_UNITS_LEFT );
    m_plotManager->setTitleAxisX( bottomUnit );
    m_plotManager->setTitleAxisY( leftUnit );
    m_plotManager->updatePlot();
}

QString Profiler::_zoomToSelection(){
    QString result;
    bool valid = false;
    std::pair<double,double> range = m_plotManager->getRange( & valid );
    if ( valid ){
        double minRange = range.first;
        double maxRange = range.second;
        if ( range.first > range.second ){
            minRange = range.second;
            maxRange = range.first;
        }
        if ( minRange < maxRange ){
            result = setZoomRange( minRange, maxRange );
        }
    }
    return result;
}


Profiler::~Profiler(){
}
}
}
