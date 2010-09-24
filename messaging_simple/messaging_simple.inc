<?php
// $Id: messaging_method_mail.class.inc,v 1.1.2.1 2010/06/04 17:57:11 jareyero Exp $
/**
 * @file
 * Drupal Messaging Framework - Send_Method class file
 */

/**
 * Base class for mail sending methods
 */
class Messaging_Simple_Method extends Messaging_Send_Method {
  // Default group and address type
  public $group = 'web';
  public $address_type = 'user';
  public $type = MESSAGING_TYPE_SEND;
  public $anonymous = FALSE;
  public $format = MESSAGING_FORMAT_HTML;

  function method_name() {
    return t('Web');
  }
  function method_description() {
    return t('Don\'t send messages but display them on a user account tab');
  }
  /**
   * It doesn't support anonymous destinations
   */
  function supports_anonymous() {
    return FALSE;
  }
  /**
   * Send message to address, use sending callback
   */
  function send_address($address, $message) {
    return $this->send_user($user, $message); 
  }
  /**
   * Send to user. Just store the message to be shown later.
   */
  function send_user($user, $message) {
    // Store without changing the original message
    $message->render();
    $store = clone $message;
    $store->uid = $user->uid;
    $store->sent = time();
    drupal_write_record('messaging_simple', $store);
    return TRUE;
  }
  
}

/**
      $info['simple'] = array(
        'class' => 'Messaging_Simple_Method',
        'name' => t('Web'),
        'group' => 'web',
        'address_type' => 'user', // Which kind of address this method uses
        'type' => MESSAGING_TYPE_SEND | MESSAGING_TYPE_NOQUEUE,
        'glue' => '<br />',
        'description' => ,
        'send callback' => 'messaging_simple_send_msg',
        'format' => 'HTML',
        'filter' => 'messaging_filter',
        'anonymous' => FALSE, // This method is not valid for anonymous users       
      );
      */