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
  public $method = 'simple';
  public $type = 'web';
  public $anonymous = FALSE;
  public $format = MESSAGING_FORMAT_HTML;

  /**
   * Get address type
   */
  public static function address_type() {
    return 'user';
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
  function send_address($user, $message) {
    return $this->send_user($user, $message); 
  }
  /**
   * Send to user. Just store the message to be shown later.
   */
  function send_user($user, $message) {
    $user = messaging_user_object($user);
    $message->render();
    $store = clone $message;
    $store->uid = $user->uid;
    $store->sent = time();
    debug($store);
    drupal_write_record('messaging_simple', $store);
    return TRUE;
  }
}
