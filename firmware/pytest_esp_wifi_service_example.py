# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

"""Check Wi-Fi service creation and startup log messages on an example firmware image."""

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_idf.utils import idf_parametrize


@pytest.mark.generic
@idf_parametrize('target', ['esp32', 'esp32s3'], indirect=['target'])
def test_esp_wifi_service_example(dut: IdfDut) -> None:
    """Require log messages reporting the service agent count and startup."""
    dut.expect(r'Create Wi-Fi service skeleton with \d+ agents', timeout=120)
    dut.expect(r'Start Wi-Fi service', timeout=60)
