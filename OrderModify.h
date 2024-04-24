#pragma once

#include "Order.h"

class OrderModify
{
public:
    OrderModify(OrderId orderId, Price price, Side side, Quantity quantity): orderId_(orderId), price_(price), side_(side), quantity_(quantity) {}

    OrderId getOrderId() const { return orderId_; }
    Price getPrice() const { return price_; }
    Side getSide() const { return side_; }
    Quantity getQuantity() const { return quantity_; }

    OrderPointer toOrderPointer(OrderType type) const
    {
        return std::make_shared<Order>(type, orderId_, side_, price_, quantity_);
    }

private:
    OrderId orderId_;
    Price price_;
    Side side_;
    Quantity quantity_;

};