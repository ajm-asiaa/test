/***
 * Main class that manages the data state for the views.
 *
 */

#pragma once

#include "State/StateInterface.h"
#include "State/ObjectManager.h"
#include <QVector>
#include <QObject>

namespace Carta {

namespace Data {

class Animator;
class Controller;
class DataLoader;
class Histogram;
class Colormap;
class Layout;
class Statistics;
class ViewPlugins;

class ViewManager : public QObject, public CartaObject {

    Q_OBJECT

public:
    /**
     * Return the unique server side id of the object with the given name and index in the
     * layout.
     * @param pluginName an identifier for the kind of object.
     * @param index an index in the case where there is more than one object of the given kind
     *      in the layout.
     */
    QString getObjectId( const QString& pluginName, int index, bool forceCreate = false );

    /**
     * Link a source plugin to a destination plugin.
     * @param sourceId the unique server-side id for the plugin that is the source of the link.
     * @param destId the unique server side id for the plugin that is the destination of the link.
     * @return an error message if the link does not succeed.
     */
    QString linkAdd( const QString& sourceId, const QString& destId );

    /**
     * Remove a link from a source to a destination.
     * @param sourceId the unique server-side id for the plugin that is the source of the link.
     * @param destId the unique server side id for the plugin that is the destination of the link.
     * @return an error message if the link does not succeed.
     */
    QString linkRemove( const QString& sourceId, const QString& destId );

    /**
     * Load the file into the controller with the given id.
     * @param fileName a locater for the data to load.
     * @param objectId the unique server side id of the controller which is responsible for displaying
     *      the file.
     */
    void loadFile( const QString& objectId, const QString& fileName);


    /**
     * Reset the layout to a predefined analysis view.
     */
    void setAnalysisView();

    /**
     * Reset the layout to show objects under active development.
     */
    void setDeveloperView();

    /**
     * Change the color map to the map with the given name.
     * @param colormapId the unique server-side id of a Colormap object.
     * @param colormapName a unique identifier for the color map to be displayed.
     */
    bool setColorMap( const QString& colormapId, const QString& colormapName );

    /**
     * Reset the layout to a predefined view displaying only a single image.
     */
    void setImageView();

    /**
     * Set the list of plugins to be displayed.
     * @param names a list of identifiers for the plugins.
     */
    bool setPlugins( const QStringList& names );

    static const QString CLASS_NAME;

    /**
     * Destructor.
     */
    virtual ~ViewManager();
private slots:
    void _pluginsChanged( const QStringList& names, const QStringList& oldNames );
private:
    ViewManager( const QString& path, const QString& id);
    class Factory;
    void _adjustSize( int count, const QString& name, const QVector<int>& insertionIndices);
    void _clear();
    void _clearAnimators( int startIndex );
    void _clearColormaps( int startIndex );
    void _clearControllers( int startIndex );
    void _clearHistograms( int startIndex );
    void _clearStatistics( int startIndex );

    int _findColorMap( const QString& id ) const;

    void _initCallbacks();

    //Sets up a default set of states for constructing the UI if the user
    //has not saved one.
    void _initializeDefaultState();

    QString _makeAnimator( int index );
    QString _makeLayout();
    QString _makePluginList();
    QString _makeController( int index );
    QString _makeHistogram( int index );
    QString _makeColorMap( int index );
    QString _makeStatistics( int index );
    void _makeDataLoader();


    void _removeView( const QString& plugin, int index );
    /**
     * Written because there is no guarantee what order the javascript side will use
     * to create view objects.  When there are linked views, the links may not get
     * recorded if one object is to be linked with one not yet created.  This flushes
     * the state and gives the object a second chance to establish their links.
     */
    void _refreshState();

    /**
     * Read and restore state for a particular sessionId from a file.
     * @param sessionId - an identifier for a user session.
     * @param fileName - the name of a saved session state.
     * @return true if the state was read and restored; false otherwise.
     */
    bool _readState( const QString& sessionId, const QString& fileName );

    /**
     * Read and restore the layout state for a particular sessionId from a file.
     * @param sessionId - an identifier for a user session.
     * @param saveName - the name of a file containing the layout state.
     * @return true if the layout state was read and restored; false otherwise.
     */
    bool _readStateLayout( const QString& sessionId, const QString& saveName );

    /**
     * Read and restore state for a particular sessionId from a string.
     * @param stateStr - a string representation of the state.
     * @param type - the type of state.
     * @return true if the state was read and restored; false otherwise.
     */
    bool _readState( const QString& stateStr, SnapshotType type );

    /**
     * Save the current state.
     * @param fileName - an identifier for the state to be saved.
     * @param layoutSave - true if the layout should be saved; false otherwise.
     * @param preferencesSave -true if the preferences should be saved; false otherwise.
     * @param dataSave - true if the data should be saved; false otherwise.
     * @return an error message if there was a problem saving state; an empty string otherwise.
     */
    QString saveState( const QString& sessionId, const QString& fileName, bool layoutSave, bool preferencesSave, bool dataSave );

    //A list of Controllers requested by the client.
    QList <Controller* > m_controllers;

    //A list of Animators requested by the client.
    QList < Animator* > m_animators;

    //Colormap
    QList<Colormap* >m_colormaps;

    //Histogram
    QList<Histogram* >m_histograms;

    //Statistics
    QList<Statistics* > m_statistics;

    static bool m_registered;
    Layout* m_layout;
    DataLoader* m_dataLoader;
    ViewPlugins* m_pluginsLoaded;

    const static QString SOURCE_ID;
    const static QString DEST_ID;

    ViewManager( const ViewManager& other);
    ViewManager operator=( const ViewManager& other );
};

}
}