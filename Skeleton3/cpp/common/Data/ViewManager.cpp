#include "Data/ViewManager.h"
#include "Data/Animator.h"
#include "Data/Controller.h"
#include "Data/DataLoader.h"
#include "Data/Layout.h"
#include "Data/ViewPlugins.h"
#include "Data/Util.h"

#include <QDir>
#include <QDebug>

const QString ViewManager::CLASS_NAME = "edu.nrao.carta.ViewManager";
bool ViewManager::m_registered =
    ObjectManager::objectManager()->registerClass ( CLASS_NAME,
                                                   new ViewManager::Factory());

ViewManager::ViewManager( const QString& path, const QString& id)
    : CartaObject( CLASS_NAME, path, id ),
      m_layout( nullptr ),
      m_dataLoader( nullptr ),
      m_pluginsLoaded( nullptr ){
    _initCallbacks();
    _initializeDataLoader();

    bool stateRead = ObjectManager::objectManager()->readState( "DefaultState" );
    if ( !stateRead ){
        _initializeDefaultState();
    }
}

void ViewManager::_initCallbacks(){
    addCommandCallback( "clearLayout", [=] (const QString & /*cmd*/,
                const QString & /*params*/, const QString & /*sessionId*/) -> QString {
        int controlCount = m_controllers.size();
        for ( int i = 0; i < controlCount; i++ ){
            m_controllers[i]->clear();
        }
        m_controllers.clear();
        m_animators.clear();
        m_layout->clear();
        return "";
    });

    //Callback for adding a data source to a Controller.
    addCommandCallback( "dataLoaded", [=] (const QString & /*cmd*/,
            const QString & params, const QString & sessionId) -> QString {
        QList<QString> keys = {"id", "data"};
        QVector<QString> dataValues = Util::parseParamMap( params, keys );
        if ( dataValues.size() == keys.size()){
            for ( int i = 0; i < m_controllers.size(); i++ ){
                if ( dataValues[0]  == m_controllers[i]->getPath() ){
                    //Add the data to it.
                    QString path = dataValues[1];
                    _initializeDataLoader();
                    path = m_dataLoader->getFile( path, sessionId );
                    m_controllers[i]->addData( path );
                    break;
                }
            }
        }
        return "";
    });

    //Callback for registering a view.
    addCommandCallback( "registerView", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QList<QString> keys = {"pluginId", "index"};
        QVector<QString> dataValues = Util::parseParamMap( params, keys );
        QString viewId( "" );
        if ( dataValues.size() == keys.size()){
            if ( dataValues[0] == "CasaImageLoader"){
                bool validIndex = false;
                int dataIndex = dataValues[1].toInt( &validIndex );
                if ( validIndex  && 0 <= dataIndex && dataIndex < m_controllers.size()){
                    viewId = m_controllers[dataIndex]->getPath();
                }
                else {
                    viewId = _makeController();
                }
            }
            else if ( dataValues[0] == "animator"){
                bool validIndex = false;
                int animIndex = dataValues[1].toInt( &validIndex );
                if ( validIndex && 0 <= animIndex && animIndex < m_animators.size()){
                    viewId = m_animators[animIndex]->getPath();
                }
                else {
                    viewId = _makeAnimator();
                }
            }
            else if ( dataValues[0] == "plugins"){
                viewId = _makePluginList();
            }
        }
        return viewId;
    });

    //Callback for linking an animator with whatever it is going to animate.
    addCommandCallback( "linkAnimator", [=] (const QString & /*cmd*/,
            const QString & params, const QString & /*sessionId*/) -> QString {
        QList<QString> keys = {"animId", "winId"};
        QVector<QString> dataValues = Util::parseParamMap( params, keys );
        if ( dataValues.size() == keys.size()){
            //Go through our data animators and find the one that is supposed to
            //be hooked up to.
            for (int i = 0; i < m_animators.size(); i++ ){
                if ( m_animators[i]->getPath()  == dataValues[0] ){
                    //Hook up the corresponding controller
                    for ( int j = 0; j < m_controllers.size(); j++ ){
                        if ( m_controllers[j]->getPath() == dataValues[1] ){
                            m_animators[i]->addController( m_controllers[j]);
                            break;
                        }
                    }
                    break;
                }
            }
        }
        return "";
    });


    //Callback for saving state.
    addCommandCallback( "saveState", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
        QStringList paramList = params.split( ":");
        QString saveName="DefaultState";
        if ( paramList.length() == 2 ){
           saveName = paramList[1];
        }
        bool result = ObjectManager::objectManager()->saveState(saveName);
        QString returnVal = "State was successfully saved.";
        if ( !result ){
            returnVal = "There was an error saving state.";
        }
        return returnVal;
    });

    //Callback for restoring state.
    addCommandCallback( "restoreState", [=] (const QString & /*cmd*/,
                const QString & params, const QString & /*sessionId*/) -> QString {
        QStringList paramList = params.split( ":");
        QString saveName="DefaultState";
        if ( paramList.length() == 2 ){
            saveName = paramList[1];
        }

        bool result = ObjectManager::objectManager()->readState(saveName);
        QString returnVal = "State was successfully restored.";
        if ( !result ){
            returnVal = "There was an error restoring state.";
        }
        return returnVal;
    });

}

QString ViewManager::_makeController(){
    ObjectManager* objManager = ObjectManager::objectManager();
    QString viewId = objManager->createObject( Controller::CLASS_NAME );
    CartaObject* controlObj = objManager->getObject( viewId );
    std::shared_ptr<Controller> target( dynamic_cast<Controller*>(controlObj) );
    m_controllers.append(target);
    return target->getPath();
}

QString ViewManager::_makeAnimator(){
    ObjectManager* objManager = ObjectManager::objectManager();
    QString viewId = objManager->createObject( Animator::CLASS_NAME );
    CartaObject* animObj = objManager->getObject( viewId );
    std::shared_ptr<Animator> target( dynamic_cast<Animator*>(animObj) );
    m_animators.append(target);
    int lastIndex = m_animators.size() - 1;
    _initializeExistingAnimationLinks( lastIndex );
    return target->getPath();
}

QString ViewManager::_makePluginList(){
    if ( !m_pluginsLoaded ){
        //Initialize a view showing the plugins that have been loaded
        ObjectManager* objManager = ObjectManager::objectManager();
        QString pluginsId = objManager->createObject( ViewPlugins::CLASS_NAME );
        CartaObject* pluginsObj = objManager->getObject( pluginsId );
        m_pluginsLoaded.reset( dynamic_cast<ViewPlugins*>(pluginsObj ));
    }
    QString pluginsPath = m_pluginsLoaded->getPath();
    return pluginsPath;
}

void ViewManager::_initializeDefaultState(){
    ObjectManager* objManager = ObjectManager::objectManager();
    _makeAnimator();
    _makeController();
    m_animators[0]->addController( m_controllers[0]);


    //Make a layout and initialize a default state.
    QString layoutId = objManager->createObject( Layout::CLASS_NAME );
    CartaObject* layoutObj = objManager->getObject( layoutId );
    m_layout.reset( dynamic_cast<Layout*>(layoutObj ));

    _makePluginList();



    /*auto & globals = * Globals::instance();
    auto connector = globals.connector();

    //Animator state.
    QString defaultAnimState("\"animator\" :{\"linkCount\" : 1,\"link\": [\"win0\"]\"channel\": {\"frameRate\" : 20,\"frameStep\" : 1,\"endBehavior\" : \"Wrap\" } }");
    const QString animId( "win3");
    connector->setState( StateKey::ANIMATOR, "win3", defaultAnimState );*/


}

void ViewManager::_initializeDataLoader(){
    if ( !m_dataLoader ){
        ObjectManager* objManager = ObjectManager::objectManager();
        QString dataLoaderId = objManager->createObject( DataLoader::CLASS_NAME );
        CartaObject* dataLoaderObj = objManager->getObject( dataLoaderId );
        m_dataLoader.reset( dynamic_cast<DataLoader*>( dataLoaderObj ));
    }
}

void ViewManager::_initializeExistingAnimationLinks( int index ){
    int linkCount = m_animators[index]->getLinkCount();
    for ( int i = 0; i < linkCount; i++ ){
        QString controllerId = m_animators[index]->getLinkId( i );
        for ( int j = 0; j < m_controllers.size(); j++ ){
            if ( controllerId == m_controllers[j]->getPath()){
                m_animators[index]->addController( m_controllers[j] );
                break;
            }
            else {
                qDebug() << "Register view could not find controller id="<<controllerId;
            }
        }
    }
}



