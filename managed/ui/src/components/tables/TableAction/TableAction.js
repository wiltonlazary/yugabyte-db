// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { BulkImportContainer, DropTableContainer, CreateBackupContainer, RestoreBackupContainer } from 'components/tables';
import { ImportReleaseContainer, UpdateReleaseContainer } from 'components/releases';
import {  MenuItem } from 'react-bootstrap';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBButton } from '../../common/forms/fields';

import _ from 'lodash';

export default class TableAction extends Component {
  constructor(props) {
    super(props);
    this.state = {
      showModal: false
    };
    this.openModal = this.openModal.bind(this);
    this.closeModal = this.closeModal.bind(this);
  }

  static propTypes = {
    currentRow: PropTypes.object,
    isMenuItem: PropTypes.bool,
    btnClass: PropTypes.string,
    onModalSubmit: PropTypes.func,
    actionType: PropTypes.oneOf(['drop', 'import', 'create-backup', 'restore-backup',
      'import-release', 'active-release', 'disable-release', 'delete-release'])
  };

  static defaultProps = {
    isMenuItem: true,
    btnClass: 'btn-default'
  };

  openModal() {
    this.setState((prevState, props) => {
      return {
        selectedRow: props.currentRow,
        showModal: true
      };
    });
  }

  closeModal() {
    this.setState((prevState, props) => {
      return {
        showModal: false
      };
    });
  }

  render() {
    const { actionType, isMenuItem, disabled } = this.props;
    let modalContainer = null;
    let btnLabel = null;
    let btnIcon = null;
    if (actionType === "drop") {
      btnLabel = "Drop Table";
      btnIcon = "fa fa-trash";
      modalContainer = (<DropTableContainer
        visible = {this.state.showModal}
        onHide = { this.closeModal}
        tableInfo = {this.state.selectedRow}
      />);
    } else if (actionType === "import") {
      btnLabel = "Bulk Import";
      btnIcon = "fa fa-download";
      modalContainer = (<BulkImportContainer
        visible = {this.state.showModal}
        onHide = {this.closeModal}
        tableInfo = {this.state.selectedRow}
      />);
    } else if (actionType === "create-backup") {
      btnLabel = "Create Backup";
      btnIcon = "fa fa-upload";
      modalContainer = (<CreateBackupContainer
        visible={this.state.showModal}
        onHide={this.closeModal}
        tableInfo={this.state.selectedRow}
      />);
    } else if (actionType === "restore-backup") {
      btnLabel = "Restore Backup";
      btnIcon = "fa fa-download";
      modalContainer = (<RestoreBackupContainer
        visible={this.state.showModal}
        onHide={this.closeModal}
        backupInfo={this.state.selectedRow}
      />);
    } else if (actionType === "import-release") {
      btnLabel = "Import";
      btnIcon = "fa fa-upload";
      modalContainer = (<ImportReleaseContainer
        visible={this.state.showModal}
        onHide={this.closeModal}
        onModalSubmit={this.props.onModalSubmit}
      />);
    } else if (["disable-release", "delete-release", "active-release"].includes(actionType)) {
      let action;
      switch (actionType) {
        case "disable-release":
          btnLabel = "Disable";
          btnIcon = "fa fa-ban";
          action = "DISABLED";
          break;
        case "delete-release":
          btnLabel = "Delete";
          btnIcon = "fa fa-trash";
          action = "DELETED";
          break;
        case "active-release":
          btnLabel = "Active";
          btnIcon = "fa fa-check";
          action = "ACTIVE";
          break;
        default:
          break;
      }
      modalContainer = (<UpdateReleaseContainer
        visible={this.state.showModal}
        onHide={this.closeModal}
        releaseInfo={this.state.selectedRow}
        actionType={action}
        onModalSubmit={this.props.onModalSubmit}
      />);
    }

    const btnId = _.uniqueId('table_action_btn_');
    if (isMenuItem) {
      return (
        <Fragment>
          <MenuItem eventKey={btnId} onClick={disabled ? null : this.openModal}
                    disabled={disabled}>
            <YBLabelWithIcon icon={btnIcon}>
              {btnLabel}
            </YBLabelWithIcon>
          </MenuItem>
          {modalContainer}
        </Fragment>
      );
    }
    return (
      <div className={this.props.className}>
        <YBButton btnText={btnLabel} btnIcon={btnIcon}
                btnClass={'btn ' + this.props.btnClass} onClick={this.openModal} />
        {modalContainer}
      </div>
    );
  }
}
