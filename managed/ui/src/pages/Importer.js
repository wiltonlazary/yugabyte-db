// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { ImporterContainer } from '../components/importer';

class Importer extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <ImporterContainer />
      </div>
    );
  }
}

export default Importer;
