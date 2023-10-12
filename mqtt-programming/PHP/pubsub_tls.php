<?php

require('vendor/autoload.php');

use \PhpMqtt\Client\MqttClient;
use \PhpMqtt\Client\ConnectionSettings;

$server   = '******.emqxsl.com';
// TLS port
$port     = 8883;
$clientId = rand(5, 15);
$username = 'emqxtest';
$password = '******';
$clean_session = false;

$connectionSettings  = (new ConnectionSettings)
  ->setUsername($username)
  ->setPassword($password)
  ->setKeepAliveInterval(60)
  ->setConnectTimeout(3)
  ->setUseTls(true)
  ->setTlsSelfSignedAllowed(true);

$mqtt = new MqttClient($server, $port, $clientId, MqttClient::MQTT_3_1_1);

$mqtt->connect($connectionSettings, $clean_session);

$mqtt->subscribe('php/mqtt', function ($topic, $message) {
    printf("Received message on topic [%s]: %s\n", $topic, $message);
}, 0);

$payload = array(
  'from' => 'php-mqtt client',
  'date' => date('Y-m-d H:i:s')
);
$mqtt->publish('php/mqtt', json_encode($payload), 0);

$mqtt->loop(true);

$mqtt->unsubscribe('php/mqtt');

$mqtt->disconnect();
printf("Disconnect from broker: ");


