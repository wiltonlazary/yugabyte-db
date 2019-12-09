// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field, FieldArray } from 'redux-form';
import {browserHistory} from 'react-router';
import _ from 'lodash';
import { isDefinedNotNull, isNonEmptyObject, isNonEmptyString, areIntentsEqual, isEmptyObject,
  isNonEmptyArray, trimSpecialChars } from 'utils/ObjectUtils';
import {
  YBTextInput,YBTextInputWithLabel, YBSelectWithLabel, YBMultiSelectWithLabel,
  YBRadioButtonBarWithLabel, YBToggle, YBUnControlledNumericInput,
  YBControlledNumericInputWithLabel
} from 'components/common/forms/fields';
import { getPromiseState } from 'utils/PromiseUtils';
import AZSelectorTable from './AZSelectorTable';
import './UniverseForm.scss';
import AZPlacementInfo from './AZPlacementInfo';
import GFlagArrayComponent from './GFlagArrayComponent';
import { getPrimaryCluster, getReadOnlyCluster,
  getClusterByType, isKubernetesUniverse } from "../../../utils/UniverseUtils";

// Default instance types for each cloud provider
const DEFAULT_INSTANCE_TYPE_MAP = {
  'aws': 'c5.large',
  'gcp': 'n1-standard-1',
  'kubernetes': 'small'
};

// Maps API storage types to UI display options
const API_UI_STORAGE_TYPES = {
  'Scratch': 'Local Scratch',
  'Persistent': 'Persistent',
  'IO1': 'IO1',
  'GP2': 'GP2'
};

const DEFAULT_STORAGE_TYPES = {
  'AWS': 'GP2',
  'GCP': 'Scratch'
};

const initialState = {
  universeName: '',
  instanceTypeSelected: '',
  azCheckState: true,
  providerSelected: '',
  regionList: [],
  numNodes: 3,
  nodeSetViaAZList: false,
  isAZUpdating: false,
  replicationFactor: 3,
  deviceInfo: {},
  placementInfo: {},
  ybSoftwareVersion: '',
  gflags: {},
  storageType: DEFAULT_STORAGE_TYPES['AWS'],
  accessKeyCode: 'yugabyte-default',
  // Maximum Number of nodes currently in use OnPrem case
  maxNumNodes: -1,
  assignPublicIP: true,
  hasInstanceTypeChanged: false,
  useTimeSync: false,
  enableYSQL: false,
  enableNodeToNodeEncrypt: false,
  enableClientToNodeEncrypt: false
};

export default class ClusterFields extends Component {

  constructor(props) {
    super(props);
    this.providerChanged = this.providerChanged.bind(this);
    this.numNodesChanged = this.numNodesChanged.bind(this);
    this.instanceTypeChanged = this.instanceTypeChanged.bind(this);
    this.regionListChanged = this.regionListChanged.bind(this);
    this.getCurrentProvider = this.getCurrentProvider.bind(this);
    this.configureUniverseNodeList = this.configureUniverseNodeList.bind(this);
    this.handleUniverseConfigure = this.handleUniverseConfigure.bind(this);
    this.storageTypeChanged = this.storageTypeChanged.bind(this);
    this.numVolumesChanged = this.numVolumesChanged.bind(this);
    this.volumeSizeChanged = this.volumeSizeChanged.bind(this);
    this.diskIopsChanged = this.diskIopsChanged.bind(this);
    this.setDeviceInfo = this.setDeviceInfo.bind(this);
    this.toggleAssignPublicIP = this.toggleAssignPublicIP.bind(this);
    this.toggleUseTimeSync = this.toggleUseTimeSync.bind(this);
    this.toggleEnableYSQL = this.toggleEnableYSQL.bind(this);
    this.toggleEnableNodeToNodeEncrypt = this.toggleEnableNodeToNodeEncrypt.bind(this);
    this.toggleEnableClientToNodeEncrypt = this.toggleEnableClientToNodeEncrypt.bind(this);
    this.toggleEnableEncryptionAtRest = this.toggleEnableEncryptionAtRest.bind(this);
    this.handleSelectAuthConfig = this.handleSelectAuthConfig.bind(this);
    this.numNodesChangedViaAzList = this.numNodesChangedViaAzList.bind(this);
    this.replicationFactorChanged = this.replicationFactorChanged.bind(this);
    this.softwareVersionChanged = this.softwareVersionChanged.bind(this);
    this.accessKeyChanged = this.accessKeyChanged.bind(this);
    this.hasFieldChanged = this.hasFieldChanged.bind(this);
    this.getCurrentUserIntent = this.getCurrentUserIntent.bind(this);

    this.currentInstanceType = _.get(this.props.universe,
      'currentUniverse.data.universeDetails.clusters[0].userIntent.instanceType');

    if (this.props.type === "Async" && isNonEmptyObject(this.props.universe.currentUniverse.data)) {
      if (isDefinedNotNull(getReadOnlyCluster(this.props.universe.currentUniverse.data.universeDetails.clusters))) {
        this.state = {
          ...initialState,
          isReadOnlyExists: true,
          editNotAllowed: this.props.editNotAllowed
        };
      } else {
        this.state = { ...initialState, isReadOnlyExists: false, editNotAllowed: false};
      }
    } else {
      this.state = initialState;
    }
  }

  componentWillMount() {
    const { formValues, clusterType, updateFormField, type } = this.props;
    const { universe: { currentUniverse: { data: { universeDetails }}}} = this.props;
    // Set default software version in case of create
    if (isNonEmptyArray(this.props.softwareVersions) && !isNonEmptyString(this.state.ybSoftwareVersion) && type === "Create") {
      this.setState({ybSoftwareVersion: this.props.softwareVersions[0]});
      updateFormField(`${clusterType}.ybSoftwareVersion`, this.props.softwareVersions[0]);
    }

    if (isNonEmptyObject(formValues['primary']) && clusterType !== 'primary') {
      this.setState({universeName: formValues['primary'].universeName});
      updateFormField(`${clusterType}.universeName`, formValues['primary'].universeName);
    }

    // This flag will prevent configure from being fired on component load
    if (formValues && isNonEmptyObject(formValues[clusterType])) {
      this.setState({nodeSetViaAZList: true});
    }

    const isEditReadOnlyFlow = type === "Async" && this.state.isReadOnlyExists;
    if (type === "Edit" || isEditReadOnlyFlow) {
      const primaryCluster = getPrimaryCluster(universeDetails.clusters);
      const readOnlyCluster = getReadOnlyCluster(universeDetails.clusters);
      const userIntent = clusterType === "async" ? readOnlyCluster && { ...readOnlyCluster.userIntent, universeName: primaryCluster.userIntent.universeName } : primaryCluster && primaryCluster.userIntent;
      const providerUUID = userIntent && userIntent.provider;
      const encryptionAtRestEnabled = universeDetails.encryptionAtRestConfig &&
          universeDetails.encryptionAtRestConfig.enableEncryptionAtRest;
      if (userIntent && providerUUID) {
        const storageType = (userIntent.deviceInfo === null) ? null : userIntent.deviceInfo.storageType;
        this.setState({
          isKubernetesUniverse: isKubernetesUniverse(this.props.universe.currentUniverse.data),
          providerSelected: providerUUID,
          instanceTypeSelected: userIntent.instanceType,
          numNodes: userIntent.numNodes,
          replicationFactor: userIntent.replicationFactor,
          ybSoftwareVersion: userIntent.ybSoftwareVersion,
          useTimeSync: userIntent.useTimeSync,
          enableYSQL: userIntent.enableYSQL,
          enableNodeToNodeEncrypt: userIntent.enableNodeToNodeEncrypt,
          enableClientToNodeEncrypt: userIntent.enableClientToNodeEncrypt,
          enableEncryptionAtRest: encryptionAtRestEnabled,
          accessKeyCode: userIntent.accessKeyCode,
          deviceInfo: userIntent.deviceInfo,
          storageType: storageType,
          regionList: userIntent.regionList,
          volumeType: (storageType === null) ? "SSD" : "EBS" //TODO(wesley): fixme - establish volumetype/storagetype relationship
        });
      }

      this.props.getRegionListItems(providerUUID);
      this.props.getInstanceTypeListItems(providerUUID);
      if (primaryCluster.userIntent.providerType === "onprem") {
        this.props.fetchNodeInstanceList(providerUUID);
      }
      // If Edit Case Set Initial Configuration
      this.props.getExistingUniverseConfiguration(_.cloneDeep(universeDetails));
    } else {
      this.props.getKMSConfigs();
      // Repopulate the form fields when switching back to the view
      if (formValues && isNonEmptyObject(formValues[clusterType])) {
        this.setState({
          providerType: formValues[clusterType].providerType,
          providerSelected: formValues[clusterType].provider,
          numNodes: formValues[clusterType].numNodes ? formValues[clusterType].numNodes : 3,
          replicationFactor: formValues[clusterType].replicationFactor ?
            Number(formValues[clusterType].replicationFactor) : 3
        });
        if (isNonEmptyString(formValues[clusterType].provider)) {
          this.props.getInstanceTypeListItems(formValues[clusterType].provider);
          this.props.getRegionListItems(formValues[clusterType].provider);
          this.setState({instanceTypeSelected: formValues[clusterType].instanceType});

          if (formValues[clusterType].assignPublicIP) {
            // We would also default to whatever primary cluster's state for this one.
            this.setState({assignPublicIP: formValues['primary'].assignPublicIP});
          }
          if (formValues[clusterType].useTimeSync) {
            // We would also default to whatever primary cluster's state for this one.
            this.setState({useTimeSync: formValues['primary'].useTimeSync});
          }
          if (formValues[clusterType].enableYSQL) {
            // We would also default to whatever primary cluster's state for this one.
            this.setState({enableYSQL: formValues['primary'].enableYSQL});
          }
          if (formValues[clusterType].enableNodeToNodeEncrypt) {
            // We would also default to whatever primary cluster's state for this one.
            this.setState({enableNodeToNodeEncrypt: formValues['primary'].enableNodeToNodeEncrypt});
          }
          if (formValues[clusterType].enableClientToNodeEncrypt) {
            // We would also default to whatever primary cluster's state for this one.
            this.setState({enableClientToNodeEncrypt: formValues['primary'].enableClientToNodeEncrypt});
          }
        }
      } else {
        // Initialize the form values if not exists
        updateFormField(`${clusterType}.numNodes`, 3);
        updateFormField(`${clusterType}.replicationFactor`, 3);
      }
    }
  }

  componentWillReceiveProps(nextProps) {
    const { universe: { currentUniverse }, cloud: { nodeInstanceList, instanceTypes }, clusterType, formValues } = nextProps;

    const currentFormValues = formValues[clusterType];
    let providerSelected = this.state.providerSelected;
    if (isNonEmptyObject(currentFormValues) && isNonEmptyString(currentFormValues.provider)) {
      providerSelected = currentFormValues.provider;
    }

    if (nextProps.cloud.instanceTypes.data !== this.props.cloud.instanceTypes.data
      && isNonEmptyArray(nextProps.cloud.instanceTypes.data) && providerSelected) {

      if (nextProps.type === "Create" || (nextProps.type === "Async" && !this.state.isReadOnlyExists)) {
        let instanceTypeSelected = null;
        const currentProviderCode = this.getCurrentProvider(providerSelected).code;
        instanceTypeSelected = DEFAULT_INSTANCE_TYPE_MAP[currentProviderCode];
        // If we have the default instance type in the cloud instance types then we
        // use it, otherwise we pick the first one in the list and use it.
        const hasInstanceType = instanceTypes.data.find((it) => {
          return it.providerCode === currentProviderCode && it.instanceTypeCode === instanceTypeSelected;
        });
        if (!hasInstanceType) {
          instanceTypeSelected = instanceTypes.data[0].instanceTypeCode;
        }

        const instanceTypeSelectedData = instanceTypes.data.find(function (item) {
          return item.instanceTypeCode === formValues[clusterType].instanceType;
        });

        if (isNonEmptyObject(instanceTypeSelectedData)) {
          instanceTypeSelected = formValues[clusterType].instanceType;
        }

        this.props.updateFormField(`${clusterType}.instanceType`, instanceTypeSelected);
        this.setState({instanceTypeSelected: instanceTypeSelected});
        this.setDeviceInfo(instanceTypeSelected, instanceTypes.data);
      };
    }

    const currentProvider = this.getCurrentProvider(providerSelected);
    // Set default storageType once API call has completed, defaults to AWS provider if current provider is not GCP
    if (typeof currentProvider !== 'undefined' &&
      currentProvider.code === "gcp" &&
      isNonEmptyArray(nextProps.cloud.gcpTypes.data) &&
      !isNonEmptyArray(this.props.cloud.gcpTypes.data)
    ) {
      this.props.updateFormField(`${clusterType}.storageType`, DEFAULT_STORAGE_TYPES['GCP']);
      this.setState({"storageType": DEFAULT_STORAGE_TYPES['GCP']});
    } else if (isNonEmptyArray(nextProps.cloud.ebsTypes) && !isNonEmptyArray(this.props.cloud.ebsTypes)) {
      this.props.updateFormField(`${clusterType}.storageType`, DEFAULT_STORAGE_TYPES['AWS']);
      this.setState({"storageType": DEFAULT_STORAGE_TYPES['AWS']});
    }

    if (isNonEmptyArray(nextProps.softwareVersions) && isNonEmptyObject(this.props.formValues[clusterType]) && !isNonEmptyString(this.props.formValues[clusterType].ybSoftwareVersion)) {
      this.setState({ybSoftwareVersion: nextProps.softwareVersions[0]});
      this.props.updateFormField(`${clusterType}.ybSoftwareVersion`, nextProps.softwareVersions[0]);
    }

    // Form Actions on Create Universe Success
    if (getPromiseState(this.props.universe.createUniverse).isLoading() && getPromiseState(nextProps.universe.createUniverse).isSuccess()) {
      this.props.reset();
      this.props.fetchUniverseMetadata();
      this.props.fetchCustomerTasks();
      if (this.context.prevPath) {
        browserHistory.push(this.context.prevPath);
      } else {
        browserHistory.push("/universes");
      }
    }
    // Form Actions on Edit Universe Success
    if ((getPromiseState(this.props.universe.editUniverse).isLoading() && getPromiseState(nextProps.universe.editUniverse).isSuccess()) ||
        (getPromiseState(this.props.universe.addReadReplica).isLoading() && getPromiseState(nextProps.universe.addReadReplica).isSuccess()) ||
        (getPromiseState(this.props.universe.editReadReplica).isLoading() && getPromiseState(nextProps.universe.editReadReplica).isSuccess()) ||
        (getPromiseState(this.props.universe.deleteReadReplica).isLoading() && getPromiseState(nextProps.universe.deleteReadReplica).isSuccess())) {
      this.props.fetchCurrentUniverse(currentUniverse.data.universeUUID);
      this.props.fetchUniverseMetadata();
      this.props.fetchCustomerTasks();
      this.props.fetchUniverseTasks(currentUniverse.data.universeUUID);
      browserHistory.push(this.props.location.pathname);
    }
    // Form Actions on Configure Universe Success
    if (getPromiseState(this.props.universe.universeConfigTemplate).isLoading() && getPromiseState(nextProps.universe.universeConfigTemplate).isSuccess()) {
      this.props.fetchUniverseResources(nextProps.universe.universeConfigTemplate.data);
    }
    // If nodeInstanceList changes, fetch number of available nodes
    if (getPromiseState(nodeInstanceList).isSuccess() && getPromiseState(this.props.cloud.nodeInstanceList).isLoading()) {
      let numNodesAvailable = nodeInstanceList.data.reduce(function (acc, val) {
        if (!val.inUse) {
          acc++;
        }
        return acc;
      }, 0);
      // Add Existing nodes in Universe userIntent to available nodes for calculation in case of Edit
      if (this.props.type === "Edit" || (nextProps.type === "Async" && !this.state.isReadOnlyExists)) {
        const cluster = getClusterByType(currentUniverse.data.universeDetails.clusters, clusterType);
        if (isDefinedNotNull(cluster)) {
          numNodesAvailable += cluster.userIntent.numNodes;
        }
      }
      this.setState({maxNumNodes: numNodesAvailable});
    }
  }

  componentDidUpdate(prevProps, prevState) {
    const {
      universe: {
        currentUniverse,
        universeConfigTemplate
      },
      formValues,
      clusterType,
      setPlacementStatus,
      toggleDisableSubmit,
      type
    } = this.props;
    let currentProviderUUID = this.state.providerSelected;
    const self = this;

    if (isNonEmptyObject(formValues[clusterType]) && isNonEmptyString(formValues[clusterType].provider)) {
      currentProviderUUID = formValues[clusterType].provider;
    }
    const currentProvider = this.getCurrentProvider(currentProviderUUID);

    const configureIntentValid = function() {
      return (!_.isEqual(self.state, prevState)) &&
        isNonEmptyObject(currentProvider) &&
        isNonEmptyArray(formValues[clusterType].regionList) &&
        (prevState.maxNumNodes !== -1 || currentProvider.code !== "onprem") &&
        ((currentProvider.code === "onprem" &&
        self.state.numNodes <= self.state.maxNumNodes) || (currentProvider.code !== "onprem")) &&
        self.state.numNodes >= self.state.replicationFactor &&
        !self.state.nodeSetViaAZList;
    };

    // Fire Configure only if either provider is not on-prem or maxNumNodes is not -1 if on-prem
    if (configureIntentValid()) {
      if (isNonEmptyObject(currentUniverse.data)) {
        if (!this.hasFieldChanged()) {
          const placementStatusObject = {
            error: {
              type: "noFieldsChanged",
              numNodes: this.state.numNodes,
              maxNumNodes: this.state.maxNumNodes
            }
          };
          setPlacementStatus(placementStatusObject);
        }
      }
      this.configureUniverseNodeList();
    } else if (currentProvider && currentProvider.code === 'onprem') {
      toggleDisableSubmit(false);
      if (isNonEmptyArray(this.state.regionList) && currentProvider &&
          this.state.instanceTypeSelected && this.state.numNodes > this.state.maxNumNodes) {
        const placementStatusObject = {
          error: {
            type: 'notEnoughNodesConfigured',
            numNodes: this.state.numNodes,
            maxNumNodes: this.state.maxNumNodes
          }
        };
        setPlacementStatus(placementStatusObject);
        toggleDisableSubmit(true);
      } else if (isNonEmptyObject(currentUniverse.data)) {
        const primaryCluster =
            currentUniverse.data.universeDetails.clusters.find(x => x.clusterType === 'PRIMARY');
        const provider = primaryCluster.placementInfo.cloudList.find(c => c.uuid === currentProvider.uuid);
        const replication = primaryCluster.userIntent.replicationFactor;
        if (provider) {
          const numAzs = provider.regionList.reduce((acc, current) => acc + current.azList.length, 0);
          setPlacementStatus({
            replicationFactor: replication,
            numUniqueAzs: numAzs,
            numUniqueRegions: provider.regionList.length
          });
        }
      }
    }
    //hook from parent universeForm to check if any fields was changed
    const nodeDetailsSet = getPromiseState(currentUniverse).isSuccess() && getPromiseState(universeConfigTemplate).isSuccess() ? universeConfigTemplate.data.nodeDetailsSet : [];
    if (type === "Edit" || (this.props.type === "Async" && this.state.isReadOnlyExists)) {
      this.props.handleHasFieldChanged(this.hasFieldChanged() || !_.isEqual(currentUniverse.data.universeDetails.nodeDetailsSet, nodeDetailsSet));
    } else {
      this.props.handleHasFieldChanged(true);
    }
  }


  numNodesChangedViaAzList(value) {
    const { updateFormField, clusterType } = this.props;
    this.setState({nodeSetViaAZList: true, numNodes: value});
    updateFormField(`${clusterType}.numNodes`, value);
  }

  setDeviceInfo(instanceTypeCode, instanceTypeList) {
    const {updateFormField, clusterType} = this.props;
    const instanceTypeSelectedData = instanceTypeList.find(function (item) {
      return item.instanceTypeCode === instanceTypeCode;
    });
    const volumesList = instanceTypeSelectedData.instanceTypeDetails.volumeDetailsList;
    const volumeDetail = volumesList[0];
    let mountPoints = null;
    if (instanceTypeSelectedData.providerCode === "onprem") {
      mountPoints = instanceTypeSelectedData.instanceTypeDetails.volumeDetailsList.map(function (item) {
        return item.mountPath;
      }).join(",");
    }
    if (volumeDetail) {
      const deviceInfo = {
        volumeSize: volumeDetail.volumeSizeGB,
        numVolumes: volumesList.length,
        mountPoints: mountPoints,
        storageType: volumeDetail.volumeType === "EBS" ? DEFAULT_STORAGE_TYPES['AWS'] : DEFAULT_STORAGE_TYPES['GCP'],
        diskIops: null
      };
      updateFormField(`${clusterType}.volumeSize`, volumeDetail.volumeSizeGB);
      updateFormField(`${clusterType}.numVolumes`, volumesList.length);
      updateFormField(`${clusterType}.diskIops`, volumeDetail.diskIops);
      updateFormField(`${clusterType}.storageType`, volumeDetail.storageType);
      updateFormField(`${clusterType}.mountPoints`, mountPoints);
      this.setState({deviceInfo: deviceInfo, volumeType: volumeDetail.volumeType});
    }
  }

  getCurrentUserIntent = () => {
    const {formValues, clusterType, universe: { currentUniverse } } = this.props;
    const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
    if (formValues[clusterType]) {
      return {
        universeName: primaryCluster.userIntent.universeName,
        numNodes: formValues[clusterType].numNodes,
        provider: formValues[clusterType].provider,
        providerType: this.getCurrentProvider(formValues[clusterType].provider) ?
          this.getCurrentProvider(formValues[clusterType].provider).code :
          null,
        regionList: formValues[clusterType].regionList.map((a)=>(a.value)),
        instanceType: formValues[clusterType].instanceType,
        ybSoftwareVersion: formValues[clusterType].ybSoftwareVersion,
        replicationFactor: formValues[clusterType].replicationFactor,
        deviceInfo: {
          volumeSize: formValues[clusterType].volumeSize,
          numVolumes: formValues[clusterType].numVolumes,
          diskIops: formValues[clusterType].diskIops,
          mountPoints: formValues[clusterType].mountPoints,
          storageType: formValues[clusterType].storageType
        },
        accessKeyCode: formValues[clusterType].accessKeyCode,
        gflags: formValues[clusterType].gflags,
        instanceTags: formValues[clusterType].instanceTags,
        useTimeSync: formValues[clusterType].useTimeSync,
        enableYSQL: formValues[clusterType].enableYSQL,
        enableNodeToNodeEncrypt: formValues[clusterType].enableNodeToNodeEncrypt,
        enableClientToNodeEncrypt: formValues[clusterType].enableClientToNodeEncrypt
      };
    }
  };

  softwareVersionChanged(value) {
    const { updateFormField, clusterType } = this.props;
    this.setState({ybSoftwareVersion: value});
    updateFormField(`${clusterType}.ybSoftwareVersion`, value);
  }

  storageTypeChanged(storageValue) {
    const { updateFormField, clusterType } = this.props;
    const currentDeviceInfo = _.clone(this.state.deviceInfo);
    currentDeviceInfo.storageType = storageValue;
    if (currentDeviceInfo.storageType === "IO1" && currentDeviceInfo.diskIops == null) {
      currentDeviceInfo.diskIops = 1000;
      updateFormField(`${clusterType}.diskIops`, 1000);
    } else {
      currentDeviceInfo.diskIops = null;
    }
    updateFormField(`${clusterType}.storageType`, storageValue);
    this.setState({deviceInfo: currentDeviceInfo, storageType: storageValue});
  }

  numVolumesChanged(val) {
    const {updateFormField, clusterType} = this.props;
    updateFormField(`${clusterType}.numVolumes`, val);
    this.setState({deviceInfo: {...this.state.deviceInfo, numVolumes: val}});
  }

  volumeSizeChanged(val) {
    const {updateFormField, clusterType} = this.props;
    updateFormField(`${clusterType}.volumeSize`, val);
    this.setState({deviceInfo: {...this.state.deviceInfo, volumeSize: val}});
  }

  diskIopsChanged(val) {
    const {updateFormField, clusterType} = this.props;
    updateFormField(`${clusterType}.diskIops`, val);
    if (this.state.deviceInfo.storageType === "IO1") {
      this.setState({deviceInfo: {...this.state.deviceInfo, diskIops: val}});
    }
  }

  toggleUseTimeSync(event) {
    const {updateFormField, clusterType} = this.props;
    updateFormField(`${clusterType}.useTimeSync`, event.target.checked);
    this.setState({useTimeSync: event.target.checked});
  }


  toggleAssignPublicIP(event) {
    const { updateFormField, clusterType } = this.props;
    // Right now we only let primary cluster to update this flag, and
    // keep the async cluster to use the same value as primary.
    if (clusterType === "primary") {
      updateFormField('primary.assignPublicIP', event.target.checked);
      updateFormField('async.assignPublicIP', event.target.checked);
      this.setState({assignPublicIP: event.target.checked});
    }
  }

  toggleEnableYSQL(event) {
    const { updateFormField, clusterType } = this.props;
    // Right now we only let primary cluster to update this flag, and
    // keep the async cluster to use the same value as primary.
    if (clusterType === "primary") {
      updateFormField('primary.enableYSQL', event.target.checked);
      updateFormField('async.enableYSQL', event.target.checked);
      this.setState({enableYSQL: event.target.checked});
    }
  }

  toggleEnableNodeToNodeEncrypt(event) {
    const { updateFormField, clusterType } = this.props;
    // Right now we only let primary cluster to update this flag, and
    // keep the async cluster to use the same value as primary.
    if (clusterType === "primary") {
      updateFormField('primary.enableNodeToNodeEncrypt', event.target.checked);
      updateFormField('async.NodeToNodeEncrypt', event.target.checked);
      this.setState({enableNodeToNodeEncrypt: event.target.checked});
    }
  }

  toggleEnableClientToNodeEncrypt(event) {
    const { updateFormField, clusterType } = this.props;
    // Right now we only let primary cluster to update this flag, and
    // keep the async cluster to use the same value as primary.
    if (clusterType === "primary") {
      updateFormField('primary.enableClientToNodeEncrypt', event.target.checked);
      updateFormField('async.ClientToNodeEncrypt', event.target.checked);
      this.setState({enableClientToNodeEncrypt: event.target.checked});
    }
  }

  toggleEnableEncryptionAtRest(event) {
    const { updateFormField, clusterType } = this.props;
    if (clusterType === "primary") {
      updateFormField('primary.enableEncryptionAtRest', event.target.checked);
      this.setState({enableEncryptionAtRest: event.target.checked});
    }
  }

  handleSelectAuthConfig(value) {
    const { updateFormField, clusterType } = this.props;
    updateFormField(`${clusterType}.selectEncryptionAtRestConfig`, value);
    this.setState({selectEncryptionAtRestConfig: value});
  }

  replicationFactorChanged = value => {
    const { updateFormField, clusterType, universe: { currentUniverse: { data }}} = this.props;
    const clusterExists = isDefinedNotNull(data.universeDetails) ? isEmptyObject(getClusterByType(data.universeDetails.clusters, clusterType)) : null;
    const self = this;

    if (!clusterExists) {
      this.setState({nodeSetViaAZList: false, replicationFactor: value}, function () {
        if (self.state.numNodes <= value) {
          self.setState({numNodes: value});
          updateFormField(`${clusterType}.numNodes`, value);
        }
      });
    }
    updateFormField(`${clusterType}.replicationFactor`, value);
  };

  hasFieldChanged = () => {
    const { universe: { currentUniverse }, clusterType } = this.props;
    if (isEmptyObject(currentUniverse.data) || isEmptyObject(currentUniverse.data.universeDetails)) {
      return true;
    }
    const currentCluster = getClusterByType(currentUniverse.data.universeDetails.clusters, clusterType);
    const existingIntent = isNonEmptyObject(currentCluster) ?
      _.clone(currentCluster.userIntent, true) : null;
    const currentIntent = this.getCurrentUserIntent();

    return !areIntentsEqual(existingIntent, currentIntent);
  };

  handleUniverseConfigure(universeTaskParams) {
    const { universe: { universeConfigTemplate, currentUniverse }, formValues, clusterType } = this.props;

    const instanceType = formValues[clusterType].instanceType;
    const regionList = formValues[clusterType].regionList;
    const verifyIntentConditions = function() {
      return isNonEmptyArray(regionList) && isNonEmptyString(instanceType);
    };

    if (verifyIntentConditions() ) {
      if (isNonEmptyObject(currentUniverse.data) && isNonEmptyObject(currentUniverse.data.universeDetails) && isDefinedNotNull(getClusterByType(currentUniverse.data.universeDetails.clusters, clusterType))) {
        // cluster set: main edit flow
        const oldCluster = getClusterByType(currentUniverse.data.universeDetails.clusters, clusterType);
        const newCluster = getClusterByType(universeTaskParams.clusters, clusterType);
        if (isNonEmptyObject(oldCluster) && isNonEmptyObject(newCluster) &&
          areIntentsEqual(oldCluster.userIntent, newCluster.userIntent)) {
          this.props.getExistingUniverseConfiguration(currentUniverse.data.universeDetails);
        } else {
          this.props.submitConfigureUniverse(universeTaskParams);
        }
      } else {
        // Create flow
        if (isEmptyObject(universeConfigTemplate.data) || universeConfigTemplate.data == null) {
          this.props.submitConfigureUniverse(universeTaskParams);
        } else {
          const currentClusterConfiguration = getClusterByType(universeConfigTemplate.data.clusters, clusterType);
          if(!isDefinedNotNull(currentClusterConfiguration)) {
            this.props.submitConfigureUniverse(universeTaskParams);
          } else if (!areIntentsEqual(
            getClusterByType(universeTaskParams.clusters, clusterType).userIntent,
            currentClusterConfiguration.userIntent) ) {
            this.props.submitConfigureUniverse(universeTaskParams);
          }
        }
      }
    }
  }

  updateTaskParams = (universeTaskParams, userIntent, clusterType) => {
    const cluster = getClusterByType(universeTaskParams.clusters, clusterType);
    universeTaskParams.currentClusterType = clusterType.toUpperCase();
    const isEdit = this.props.type === "Edit" ||
      (this.props.type === "Async" && this.state.isReadOnlyExists);

    if (isDefinedNotNull(cluster)) {
      cluster.userIntent = userIntent;
    } else {
      if (isEmptyObject(universeTaskParams.clusters)) {
        universeTaskParams.clusters = [];
      }
      universeTaskParams.clusters.push({
        clusterType: clusterType.toUpperCase(),
        userIntent: userIntent
      });
    }
    universeTaskParams.clusterOperation = isEdit ? "EDIT": "CREATE";
  }

  configureUniverseNodeList() {
    const { universe: { universeConfigTemplate, currentUniverse }, formValues, clusterType } = this.props;
    const { hasInstanceTypeChanged } = this.state;
    const currentProviderUUID = this.state.providerSelected;
    let universeTaskParams = {};
    if (isNonEmptyObject(universeConfigTemplate.data)) {
      universeTaskParams = _.cloneDeep(universeConfigTemplate.data);
    }
    if (this.props.type === "Async" && !isDefinedNotNull(getReadOnlyCluster(currentUniverse.data.universeDetails.clusters))) {
      universeTaskParams = _.cloneDeep(currentUniverse.data.universeDetails);
    }
    if (isNonEmptyObject(currentUniverse.data)) {
      universeTaskParams.universeUUID = currentUniverse.data.universeUUID;
      universeTaskParams.expectedUniverseVersion = currentUniverse.data.version;
    }

    const userIntent = {
      universeName: formValues[clusterType].universeName,
      provider: formValues[clusterType].provider,
      regionList: formValues[clusterType].regionList && formValues[clusterType].regionList.map(function (item) {
        return item.value;
      }),
      assignPublicIP: formValues[clusterType].assignPublicIP,
      useTimeSync: formValues[clusterType].useTimeSync,
      enableYSQL: formValues[clusterType].enableYSQL,
      enableNodeToNodeEncrypt: formValues[clusterType].enableNodeToNodeEncrypt,
      enableClientToNodeEncrypt: formValues[clusterType].enableClientToNodeEncrypt,
      numNodes: formValues[clusterType].numNodes,
      instanceType: formValues[clusterType].instanceType,
      ybSoftwareVersion: formValues[clusterType].ybSoftwareVersion,
      replicationFactor: formValues[clusterType].replicationFactor,
      deviceInfo: {
        volumeSize: formValues[clusterType].volumeSize,
        numVolumes: formValues[clusterType].numVolumes,
        mountPoints: formValues[clusterType].mountPoints,
        storageType: formValues[clusterType].storageType,
        diskIops: formValues[clusterType].diskIops
      },
      accessKeyCode: formValues[clusterType].accessKeyCode
    };

    if (hasInstanceTypeChanged !==
      (formValues[clusterType].instanceType !== this.currentInstanceType)
    ) {
      this.setState({ hasInstanceTypeChanged: !hasInstanceTypeChanged });
    }

    if (isNonEmptyObject(formValues[clusterType].masterGFlags)) {
      userIntent["masterGFlags"] = formValues[clusterType].masterGFlags;
    }
    if (isNonEmptyObject(formValues[clusterType].tserverGFlags)) {
      userIntent["tserverGFlags"] = formValues[clusterType].tserverGFlags;
    }
    if (isNonEmptyObject(formValues[clusterType].instanceTags) && currentProviderUUID && this.getCurrentProvider(currentProviderUUID).code === "aws") {
      userIntent["instanceTags"] = formValues[clusterType].instanceTags;
    }

    this.props.cloud.providers.data.forEach(function (providerItem) {
      if (providerItem.uuid === formValues[clusterType].provider) {
        userIntent.providerType = providerItem.code;
      }
    });

    this.updateTaskParams(universeTaskParams, userIntent, clusterType);
    universeTaskParams.userAZSelected = false;
    this.handleUniverseConfigure(universeTaskParams);
  }

  numNodesChanged(value) {
    const {updateFormField, clusterType} = this.props;
    this.setState({numNodes: value, nodeSetViaAZList: false});
    updateFormField(`${clusterType}.numNodes`, value);
  }

  getCurrentProvider(providerUUID) {
    return this.props.cloud.providers.data.find((provider) => provider.uuid === providerUUID);
  }

  providerChanged = (value) => {
    const {updateFormField, clusterType, universe: {currentUniverse: {data}}} = this.props;
    const providerUUID = value;
    const currentProviderData = this.getCurrentProvider(value) || {};

    const targetCluster = clusterType !== "primary" ? isNonEmptyObject(data) && getPrimaryCluster(data.universeDetails.clusters) : isNonEmptyObject(data) && getReadOnlyCluster(data.universeDetails.clusters);
    if (isEmptyObject(data) || isDefinedNotNull(targetCluster)) {
      this.props.updateFormField(`${clusterType}.regionList`, []);
      //If we have accesskeys for a current selected provider we set that in the state or we fallback to default value.
      let defaultAccessKeyCode = initialState.accessKeyCode;
      if (isNonEmptyArray(this.props.accessKeys.data)) {
        const providerAccessKeys = this.props.accessKeys.data.filter((key) => key.idKey.providerUUID === value);
        if (isNonEmptyArray(providerAccessKeys)) {
          defaultAccessKeyCode = providerAccessKeys[0].idKey.keyCode;
        }
      }
      updateFormField(`${clusterType}.accessKeyCode`, defaultAccessKeyCode);

      if (currentProviderData.code === "gcp") {
        this.storageTypeChanged(DEFAULT_STORAGE_TYPES['GCP']);
      } else if (currentProviderData.code === "aws") {
        this.storageTypeChanged(DEFAULT_STORAGE_TYPES['AWS']);
      }

      this.setState({nodeSetViaAZList: false, regionList: [], providerSelected: providerUUID,
        deviceInfo: {}, accessKeyCode: defaultAccessKeyCode});
      this.props.getRegionListItems(providerUUID, true);
      this.props.getInstanceTypeListItems(providerUUID);
    }

    if (currentProviderData.code === "onprem") {
      this.props.fetchNodeInstanceList(value);
    }
    this.setState({
      isKubernetesUniverse: currentProviderData.code === "kubernetes"
    });
  }

  accessKeyChanged(event) {
    const {clusterType} = this.props;
    this.props.updateFormField(`${clusterType}.accessKeyCode`, event.target.value);
  }

  instanceTypeChanged(value) {
    const {updateFormField, clusterType} = this.props;
    const instanceTypeValue = value;
    updateFormField(`${clusterType}.instanceType`, instanceTypeValue);
    this.setState({instanceTypeSelected: instanceTypeValue, nodeSetViaAZList: false});

    this.setDeviceInfo(instanceTypeValue, this.props.cloud.instanceTypes.data);
  }

  regionListChanged(value) {
    const {formValues, clusterType, updateFormField, cloud:{providers}} = this.props;
    this.setState({nodeSetViaAZList: false, regionList: value});
    const currentProvider = providers.data.find((a)=>(a.uuid === formValues[clusterType].provider));
    if (!isNonEmptyString(formValues[clusterType].instanceType)) {
      updateFormField(`${clusterType}.instanceType`, DEFAULT_INSTANCE_TYPE_MAP[currentProvider.code]);
    }
  }

  render() {
    const {clusterType, cloud, softwareVersions, accessKeys, universe, formValues} = this.props;
    const { hasInstanceTypeChanged } = this.state;
    const self = this;
    let gflagArray = <span/>;
    let tagsArray = <span/>;
    let universeProviderList = [];
    let currentProviderCode = "";

    let currentProviderUUID = self.state.providerSelected;
    if (formValues[clusterType] && formValues[clusterType].provider) {
      currentProviderUUID = formValues[clusterType].provider;
    }

    // Populate the cloud provider list
    if (isNonEmptyArray(cloud.providers.data)) {
      universeProviderList = cloud.providers.data.map(function(providerItem, idx) {
        if (providerItem.uuid === currentProviderUUID) {
          currentProviderCode = providerItem.code;
        }
        return (
          <option key={providerItem.uuid} value={providerItem.uuid}>
            {providerItem.name}
          </option>
        );
      });
    }

    // Spot price and EBS types
    let storageTypeSelector = <span/>;
    let deviceDetail = null;
    let iopsField = <span/>;
    function volumeTypeFormat(num) {
      return num + ' GB';
    }
    const ebsTypesList =
      cloud.ebsTypes && cloud.ebsTypes.sort().map(function (ebsType, idx) {
        return <option key={ebsType} value={ebsType}>{ebsType}</option>;
      });
    const gcpTypesList = 
      cloud.gcpTypes.data && cloud.gcpTypes.data.sort().map(function (gcpType, idx) {
        return <option key={gcpType} value={gcpType}>{API_UI_STORAGE_TYPES[gcpType]}</option>;
      });
    const kmsConfigList = [
      <option value="0" key={`kms-option-0`}>Select Configuration</option>,
      ...cloud.authConfig.data.map((config, index) => {
        const labelName = config.metadata.provider + " - " + config.metadata.name;
        return (<option value={
          config.metadata.configUUID
        } key={`kms-option-${index + 1}`}>{labelName}</option>);
      })
    ];
    const isFieldReadOnly = isNonEmptyObject(universe.currentUniverse.data) && (this.props.type === "Edit" || (this.props.type === "Async" && this.state.isReadOnlyExists));

    //Get list of cloud providers
    const providerNameField = (<Field name={`${clusterType}.provider`} type="select" component={YBSelectWithLabel} label="Provider"
      onInputChanged={this.providerChanged} options={universeProviderList} readOnlySelect={isFieldReadOnly}/>);

    const deviceInfo = this.state.deviceInfo;

    if (isNonEmptyObject(formValues[clusterType])) {
      const currentCluster = formValues[clusterType];
      if (isNonEmptyString(currentCluster.numVolumes)) {
        deviceInfo["numVolumes"] = currentCluster.numVolumes;
      }
      if (isNonEmptyString(currentCluster.volumeSize)) {
        deviceInfo["volumeSize"] = currentCluster.volumeSize;
      }
      if (isNonEmptyString(currentCluster.diskIops)) {
        deviceInfo["diskIops"] = currentCluster.diskIops;
      }
      if (isNonEmptyObject(currentCluster.storageType)) {
        deviceInfo["storageType"] = currentCluster.storageType;
      }
    }

    if (isNonEmptyObject(deviceInfo)) {
      const currentProvider = this.getCurrentProvider(self.state.providerSelected);
      if ((self.state.volumeType === 'EBS' || self.state.volumeType === 'SSD') && isDefinedNotNull(currentProvider)) {
        const isInAws = currentProvider.code === 'aws';
        const isInGcp = currentProvider.code === 'gcp';
        // We don't want to keep the volume fixed in case of Kubernetes or persistent GCP storage.
        const fixedVolumeInfo = self.state.volumeType === 'SSD' && 
          currentProvider.code !== 'kubernetes' && deviceInfo.storageType === "Scratch";
        const fixedNumVolumes = self.state.volumeType === 'SSD' &&
          currentProvider.code !== 'kubernetes' && currentProvider.code !== 'gcp';
        const isIoType = deviceInfo.storageType === 'IO1';
        if (isIoType) {
          iopsField = (
            <span className="volume-info form-group-shrinked  volume-info-iops">
              <label className="form-item-label">Provisioned IOPS</label>
              <span className="volume-info-field">
                <Field name={`${clusterType}.diskIops`} component={YBUnControlledNumericInput}
                  label="Provisioned IOPS" onInputChanged={self.diskIopsChanged}
                  readOnly={isFieldReadOnly} />
              </span>
            </span>
          );
        }
        const numVolumes = (
          <span className="volume-info-field volume-info-count">
            <Field name={`${clusterType}.numVolumes`} component={YBUnControlledNumericInput}
              label="Number of Volumes" onInputChanged={self.numVolumesChanged}
              readOnly={fixedNumVolumes || !hasInstanceTypeChanged} />
          </span>
        );
        const volumeSize = (
          <span className="volume-info-field volume-info-size">
            <Field name={`${clusterType}.volumeSize`} component={YBUnControlledNumericInput}
              label="Volume Size" valueFormat={volumeTypeFormat} onInputChanged={self.volumeSizeChanged}
              readOnly={fixedVolumeInfo || !hasInstanceTypeChanged} />
          </span>
        );
        deviceDetail = (
          <span className="volume-info">
            {numVolumes}
            &times;
            {volumeSize}
          </span>
        );
        // Only for AWS EBS or GCP, show type option.
        if (isInAws && self.state.volumeType === "EBS") {
          storageTypeSelector = (
            <span className="volume-info form-group-shrinked">
              <Field name={`${clusterType}.storageType`} component={YBSelectWithLabel}
                options={ebsTypesList} label="EBS Type" defaultValue={DEFAULT_STORAGE_TYPES['AWS']} onInputChanged={self.storageTypeChanged}
                readOnlySelect={isFieldReadOnly} />
            </span>
          );
        } else if (isInGcp) {
          storageTypeSelector = (
            <span className="volume-info form-group-shrinked">
              <Field name={`${clusterType}.storageType`} component={YBSelectWithLabel}
                options={gcpTypesList} label="Storage Type (SSD)" defaultValue={DEFAULT_STORAGE_TYPES['GCP']} onInputChanged={self.storageTypeChanged}
                readOnlySelect={isFieldReadOnly} />
            </span>
          );
        }
      }
    }

    let assignPublicIP = <span />;
    let useTimeSync = <span />;
    let enableYSQL = <span />;
    let enableNodeToNodeEncrypt = <span />;
    let enableClientToNodeEncrypt = <span />;
    let selectTlsCert = <span />;
    let enableEncryptionAtRest = <span />;
    let selectEncryptionAtRestConfig = <span />;
    const currentProvider = this.getCurrentProvider(currentProviderUUID);

    if (isDefinedNotNull(currentProvider) &&
       (currentProvider.code === "aws" || currentProvider.code === "gcp" ||
        currentProvider.code === "onprem" || currentProvider.code === "kubernetes")){
      const disableOnChange = clusterType !== "primary";
      enableYSQL = (
        <Field name={`${clusterType}.enableYSQL`}
          component={YBToggle} isReadOnly={isFieldReadOnly}
          disableOnChange={disableOnChange}
          checkedVal={this.state.enableYSQL}
          onToggle={this.toggleEnableYSQL}
          label="Enable YSQL"
          subLabel="Whether or not to enable YSQL."/>
      );
      enableNodeToNodeEncrypt = (
        <Field name={`${clusterType}.enableNodeToNodeEncrypt`}
          component={YBToggle} isReadOnly={isFieldReadOnly}
          disableOnChange={disableOnChange}
          checkedVal={this.state.enableNodeToNodeEncrypt}
          onToggle={this.toggleEnableNodeToNodeEncrypt}
          label="Enable Node-to-Node TLS"
          subLabel="Whether or not to enable TLS Encryption for node to node communication."/>
      );
      enableClientToNodeEncrypt = (
        <Field name={`${clusterType}.enableClientToNodeEncrypt`}
          component={YBToggle} isReadOnly={isFieldReadOnly}
          disableOnChange={disableOnChange}
          checkedVal={this.state.enableClientToNodeEncrypt}
          onToggle={this.toggleEnableClientToNodeEncrypt}
          label="Enable Client-to-Node TLS"
          subLabel="Whether or not to enable TLS encryption for client to node communication."/>
      );
      enableEncryptionAtRest = (
        <Field name={`${clusterType}.enableEncryptionAtRest`}
          component={YBToggle} isReadOnly={isFieldReadOnly}
          disableOnChange={disableOnChange}
          checkedVal={this.state.enableEncryptionAtRest}
          onToggle={this.toggleEnableEncryptionAtRest}
          label="Enable Encryption at Rest"
          title="Upload encryption key file"
          subLabel="Enable encryption for data stored on tablet servers."
        />
      );

      if (this.state.enableEncryptionAtRest) {
        selectEncryptionAtRestConfig = (
          <Field name={`${clusterType}.selectEncryptionAtRestConfig`}
            component={YBSelectWithLabel}
            label="Key Management Service Config"
            options={kmsConfigList}
            input={{
              onChange: this.handleSelectAuthConfig
            }}
          />
        );
      }
    }

    { // Block scope for state variables
      const { enableClientToNodeEncrypt, enableNodeToNodeEncrypt } = this.state;
      const tlsCertOptions = [
        <option key={'cert-option-0'} value={''}>
          Create new certificate
        </option>
      ];
      if (!_.isEmpty(this.props.userCertificates.data)) {
        this.props.userCertificates.data.forEach((cert, index) => {
          tlsCertOptions.push(
            <option key={`cert-option-${index + 1}`} value={cert.uuid}>
              {cert.label}
            </option>);
        });
      }
      if (isDefinedNotNull(currentProvider) && (enableClientToNodeEncrypt || enableNodeToNodeEncrypt)) {
        const isSelectReadOnly = this.props.type === 'Edit';
        selectTlsCert = (
          <Field name={`${clusterType}.tlsCertificateId`}
            component={YBSelectWithLabel}
            options={tlsCertOptions}
            readOnlySelect={isSelectReadOnly}
            label="Root Certificate"/>
        );
      }
    }
    
    if (isDefinedNotNull(currentProvider) &&
        (currentProvider.code === "aws" || currentProvider.code === "gcp")) {
      // Assign public ip would be only enabled for primary and that same
      // value will be used for async as well.
      const disableOnChange = clusterType !== "primary";
      assignPublicIP = (
        <Field name={`${clusterType}.assignPublicIP`}
          component={YBToggle} isReadOnly={isFieldReadOnly}
          disableOnChange={disableOnChange}
          checkedVal={this.state.assignPublicIP}
          onToggle={this.toggleAssignPublicIP}
          label="Assign Public IP"
          subLabel="Whether or not to assign a public IP."/>
      );
    }
    // Only enable Time Sync Service toggle for AWS.
    if (isDefinedNotNull(currentProvider) && currentProvider.code === "aws") {
      useTimeSync = (
        <Field name={`${clusterType}.useTimeSync`}
          component={YBToggle} isReadOnly={isFieldReadOnly}
          checkedVal={this.state.useTimeSync}
          onToggle={this.toggleUseTimeSync}
          label="Use AWS Time Sync"
          subLabel="Whether or not to use the Amazon Time Sync Service."/>
      );
    }

    universeProviderList.unshift(<option key="" value=""></option>);

    let universeRegionList = [];
    if (self.state.providerSelected) {
      universeRegionList =
        cloud.regions.data && cloud.regions.data.map(function (regionItem) {
          return {value: regionItem.uuid, label: regionItem.name};
        });
    }

    let universeInstanceTypeList = <option/>;

    if (currentProviderCode === "aws") {
      const optGroups = this.props.cloud.instanceTypes && isNonEmptyArray(this.props.cloud.instanceTypes.data) && this.props.cloud.instanceTypes.data.reduce(function(groups, it) {
        const prefix = it.instanceTypeCode.substr(0, it.instanceTypeCode.indexOf("."));
        groups[prefix] ? groups[prefix].push(it.instanceTypeCode): groups[prefix] = [it.instanceTypeCode];
        return groups;
      }, {});
      if (isNonEmptyObject(optGroups)) {
        universeInstanceTypeList = Object.keys(optGroups).map(function(key, idx){
          return(
            <optgroup label={`${key.toUpperCase()} type instances`} key={key+idx}>
              {
                optGroups[key].sort((a, b) => (/\d+(?!\.)/.exec(a) - /\d+(?!\.)/.exec(b)))
                  .map((item, arrIdx) => (
                    <option key={idx+arrIdx} value={item}>
                      {item}
                    </option>
                  ))
              }
            </optgroup>
          );
        });
      }
    } else if (currentProviderCode === "kubernetes")  {
      universeInstanceTypeList =
        cloud.instanceTypes.data && cloud.instanceTypes.data.map(function (instanceTypeItem, idx) {
          return (
            <option key={instanceTypeItem.instanceTypeCode}
              value={instanceTypeItem.instanceTypeCode}>
              {instanceTypeItem.instanceTypeName || instanceTypeItem.instanceTypeCode } ({instanceTypeItem.numCores} {instanceTypeItem.numCores>1 ? "cores" : "core"}, {instanceTypeItem.memSizeGB}GB RAM)
            </option>
          );
        });
    } else {
      universeInstanceTypeList =
        cloud.instanceTypes.data && cloud.instanceTypes.data.map(function (instanceTypeItem, idx) {
          return (
            <option key={instanceTypeItem.instanceTypeCode}
              value={instanceTypeItem.instanceTypeCode}>
              {instanceTypeItem.instanceTypeCode}
            </option>
          );
        });
    }

    let placementStatus = <span/>;
    if (self.props.universe.currentPlacementStatus) {
      placementStatus = <AZPlacementInfo placementInfo={self.props.universe.currentPlacementStatus}/>;
    }
    const azSelectorTable = (
      <div>
        <AZSelectorTable {...this.props} clusterType={clusterType}
          numNodesChangedViaAzList={this.numNodesChangedViaAzList} minNumNodes={this.state.replicationFactor}
          maxNumNodes={this.state.maxNumNodes} currentProvider={this.getCurrentProvider(currentProviderUUID)} isKubernetesUniverse={this.state.isKubernetesUniverse} />
        {placementStatus}
      </div>);

    if (clusterType === "primary") {
      gflagArray =
        (<Row>
          <Col md={12}>
            <h4>G-Flags</h4>
          </Col>
          <Col md={6}>
            <FieldArray component={GFlagArrayComponent} name={`${clusterType}.masterGFlags`} flagType="master" operationType="Create" isReadOnly={isFieldReadOnly}/>
          </Col>
          <Col md={6}>
            <FieldArray component={GFlagArrayComponent} name={`${clusterType}.tserverGFlags`} flagType="tserver" operationType="Create" isReadOnly={isFieldReadOnly}/>
          </Col>
        </Row>);
    }

    if (clusterType === "primary" ) {
      tagsArray =
        (<Row>
          <Col md={12}>
            <h4>User Tags</h4>
          </Col>
          <Col md={6}>
            <FieldArray component={GFlagArrayComponent} name={`${clusterType}.instanceTags`} flagType="tag" operationType="Create" isReadOnly={false}/>
          </Col>
        </Row>);
    }

    const softwareVersionOptions = softwareVersions.map((item, idx) => (
      <option key={idx} value={item}>{item}</option>
    ));

    let accessKeyOptions = <option key={1} value={this.state.accessKeyCode}>{this.state.accessKeyCode}</option>;
    if (_.isObject(accessKeys) && isNonEmptyArray(accessKeys.data)) {
      accessKeyOptions = accessKeys.data.filter((key) => key.idKey.providerUUID === currentProviderUUID)
        .map((item, idx) => (
          <option key={idx} value={item.idKey.keyCode}>
            {item.idKey.keyCode}
          </option>));
    }
    let universeNameField = <span/>;
    if (clusterType === "primary") {
      universeNameField = (<Field name={`${clusterType}.universeName`}
        type="text" normalize={trimSpecialChars} component={YBTextInputWithLabel}
        label="Name" isReadOnly={isFieldReadOnly}/>);
    }

    return (
      <div>
        <div className="form-section" data-yb-section="cloud-config">
          <Row>
            <Col md={6}>
              <h4 style={{marginBottom: 40}}>Cloud Configuration</h4>
              {this.state.isAZUpdating}
              <div className="form-right-aligned-labels">
                {universeNameField}
                {providerNameField}
                <Field name={`${clusterType}.regionList`} component={YBMultiSelectWithLabel} options={universeRegionList}
                  label="Regions" data-yb-field="regions" isMulti={true} selectValChanged={this.regionListChanged}
                  providerSelected={currentProviderUUID}/>
                { clusterType === "async"
                  ? [<Field key="numNodes" name={`${clusterType}.numNodes`} type="text" component={YBControlledNumericInputWithLabel}
                      className={getPromiseState(this.props.universe.universeConfigTemplate).isLoading() ? "readonly" : ""}
                      data-yb-field="nodes" label={this.state.isKubernetesUniverse ? "Pods" : "Nodes"}
                      onInputChanged={this.numNodesChanged} onLabelClick={this.numNodesClicked} val={this.state.numNodes}
                      minVal={Number(this.state.replicationFactor)} />,
                    <Field key="replicationFactor" name={`${clusterType}.replicationFactor`} type="text" component={YBRadioButtonBarWithLabel} options={[1, 2, 3, 4, 5, 6, 7]}
                      label="Replication Factor" initialValue={this.state.replicationFactor} onSelect={this.replicationFactorChanged} isReadOnly={isFieldReadOnly}/>]
                  : null
                }
              </div>

              { clusterType !== "async" &&
                <Row>
                  <div className="form-right-aligned-labels">
                    <Col lg={5}>
                      <Field name={`${clusterType}.numNodes`} type="text" component={YBControlledNumericInputWithLabel} className={getPromiseState(this.props.universe.universeConfigTemplate).isLoading() ? "readonly" : ""}
                            label={this.state.isKubernetesUniverse ? "Pods" : "Nodes"} onInputChanged={this.numNodesChanged} onLabelClick={this.numNodesClicked} val={this.state.numNodes}
                            minVal={Number(this.state.replicationFactor)}/>
                    </Col>
                    <Col lg={7} className="button-group-row">
                      <Field name={`${clusterType}.replicationFactor`} type="text" component={YBRadioButtonBarWithLabel} options={[1, 3, 5, 7]}
                        label="Replication Factor" initialValue={this.state.replicationFactor} onSelect={this.replicationFactorChanged} isReadOnly={isFieldReadOnly}/>

                    </Col>
                  </div>
                </Row>
              }
            </Col>
            <Col md={6} className={"universe-az-selector-container"}>
              {azSelectorTable}
            </Col>
          </Row>
        </div>
        <div className="form-section" data-yb-section="instance-config">
          <Row>
            <Col md={12}>
              <h4>Instance Configuration</h4>
            </Col>
          </Row>
          <Row>
            <Col sm={12} md={12} lg={6}>
              <div className="form-right-aligned-labels">
                <Field name={`${clusterType}.instanceType`} component={YBSelectWithLabel} label="Instance Type"
                  options={universeInstanceTypeList} onInputChanged={this.instanceTypeChanged} />
              </div>
            </Col>
            <Col sm={12} md={12} lg={6}>
              {deviceDetail &&
                <div className="form-right-aligned-labels">
                  <div className="form-inline-controls">
                    <div className="form-group universe-form-instance-info" data-yb-field="volumn-info">
                      <label className="form-item-label form-item-label-shrink">Volume Info</label>
                      {deviceDetail}
                    </div>
                  </div>
                  <div className="form-inline-controls">
                    <div className="form-group universe-form-instance-info">
                      {storageTypeSelector}
                    </div>
                  </div>
                  <div className="form-inline-controls">
                    <div className="form-group universe-form-instance-info">
                      {iopsField}
                    </div>
                  </div>
                </div>
              }
            </Col>
            <Col sm={12} md={12} lg={6}>
              <div className="form-right-aligned-labels">
                {selectTlsCert}
                {assignPublicIP}
                {useTimeSync}
                {enableYSQL}
                {enableNodeToNodeEncrypt}
                {enableClientToNodeEncrypt}
                {enableEncryptionAtRest}
                <Field name={`${clusterType}.mountPoints`} component={YBTextInput}  type="hidden"/>
              </div>
            </Col>
            <Col sm={12} md={6} lg={4}>
              <div className="form-right-aligned-labels right-side-form-field">
                <Row>
                  {selectEncryptionAtRestConfig}
                </Row>
              </div>
            </Col>
          </Row>
        </div>
        <div className="form-section" data-yb-section="advanced">
          <Row>
            <Col md={12}>
              <h4>Advanced</h4>
            </Col>
            <Col sm={5} md={4}>
              <div className="form-right-aligned-labels">
                <Field name={`${clusterType}.ybSoftwareVersion`} component={YBSelectWithLabel}
                  options={softwareVersionOptions} label="DB Version" onInputChanged={this.softwareVersionChanged} readOnlySelect={isFieldReadOnly}/>
              </div>
            </Col>
            {!this.state.isKubernetesUniverse &&
            <Col lg={4}>
              <div className="form-right-aligned-labels">
                <Field name={`${clusterType}.accessKeyCode`} type="select" component={YBSelectWithLabel} label="Access Key"
                  onInputChanged={this.accessKeyChanged} options={accessKeyOptions} readOnlySelect={isFieldReadOnly}/>
              </div>
            </Col>
            }
          </Row>
        </div>
        <div className="form-section" data-yb-section="g-flags">
          {gflagArray}
        </div>
        {currentProviderCode === "aws" && clusterType === "primary" && <div className="form-section no-border">
          {tagsArray}
        </div>}
      </div>
    );
  }
}
