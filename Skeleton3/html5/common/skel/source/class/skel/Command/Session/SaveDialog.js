/**
 * Dialog for saving state.
 */

/*global mImport */
/*******************************************************************************
 * @ignore( mImport)
 ******************************************************************************/
qx.Class.define("skel.Command.Session.SaveDialog", {
    extend : qx.ui.core.Widget,

    /**
     * Constructor.
     */
    construct : function( ) {
        this.base(arguments);

        var nameContainer = new qx.ui.container.Composite();
        nameContainer.setLayout(new qx.ui.layout.HBox(2));
        var nameLabel = new qx.ui.basic.Label( "Save Name:");
        nameContainer.add( new qx.ui.core.Spacer(1), {flex:1});
        nameContainer.add(nameLabel);
        this.m_saveText = new qx.ui.form.TextField();
        this.m_saveText.setValue( this.m_DEFAULT_SAVE );
        skel.widgets.TestID.addTestId( this.m_saveText, "snapshotSaveName");
        nameContainer.add( this.m_saveText);
        nameContainer.add( new qx.ui.core.Spacer(1), {flex:1});
        
        var checkContainer = new qx.ui.container.Composite();
        checkContainer.setLayout( new qx.ui.layout.HBox(2));
        this.m_layoutCheck = new qx.ui.form.CheckBox("Layout");
        this.m_layoutCheck.setValue( true );
        this.m_preferencesCheck = new qx.ui.form.CheckBox("Preferences");
        //this.m_preferencesCheck.setValue( true );
        this.m_allCheck = new qx.ui.form.CheckBox("Data");
        checkContainer.add( new qx.ui.core.Spacer(1), {flex:1});
        checkContainer.add( this.m_preferencesCheck );
        checkContainer.add( this.m_layoutCheck );
        checkContainer.add( this.m_allCheck );
        checkContainer.add( new qx.ui.core.Spacer(1), {flex:1});
        
        var butContainer = new qx.ui.container.Composite();
        butContainer.setLayout( new qx.ui.layout.HBox(5));
        butContainer.add( new qx.ui.core.Spacer(1), {flex:1});
        var closeButton = new qx.ui.form.Button( "Close");
        closeButton.addListener( "execute", function(){
            this.fireDataEvent("closeSessionSave", "");
        }, this);
        
        var saveButton = new qx.ui.form.Button( "Save");
        saveButton.addListener( "execute", function(){
            console.log( "Send save command");
            var path = skel.widgets.Path.getInstance();
            var cmd = path.getCommandSaveState();
            var fileName = this.m_saveText.getValue();
            var saveLayout = this.m_layoutCheck.getValue();
            var savePreferences = this.m_preferencesCheck.getValue();
            var saveData = this.m_allCheck.getValue();
            var connector = mImport("connector");
            var params = "fileName:"+fileName+",layoutSnapshot:"+saveLayout+",preferencesSnapshot:"+savePreferences+",dataSnapshot:"+saveData;
            connector.sendCommand( cmd, params, function(val){} );
        }, this );
        butContainer.add( saveButton );
        butContainer.add( closeButton );
        
        this._setLayout( new qx.ui.layout.VBox(2));
        this._add( nameContainer );
        this._add( checkContainer );
        this._add( butContainer );
       
    },

    events : {
        "closeSessionSave" : "qx.event.type.Data"
    },

    members : {
        m_DEFAULT_SAVE : "session_default",
        m_layoutCheck : null,
        m_preferencesCheck : null,
        m_allCheck : null,
        m_saveText : null
    },

    properties : {
        appearance : {
            refine : true,
            init : "popup-dialog"
        }

    }

});